/*
 * gbaview: play a ROM in a window, with sound.
 *   gbaview <rom.gba> [--bios <file>] [--mute] [--autotest <frames> <out.png>]
 * Keys: arrows = D-pad, Z = A, X = B, A = L, S = R, Enter = Start, Backspace = Select,
 *       F12 = screenshot (gbaview-<n>.png), Esc = quit.
 * Cartridge extras: I/J/K/L tilt, Q/E turn (gyro), [ and ] light -/+ (solar); the clock is set
 * from the PC's local time. The title shows the light level (solar carts) and RUMBLE.
 * Sound plays through waveOut at 32,768 Hz and paces the emulation (about 3 frames queued).
 * With --mute, or when no audio device opens, a 59.7275 fps timer paces it instead.
 * The save file sits next to the ROM (game.gba → game.sav): loaded at start, written about one
 * second after the game last saved and when the window closes.
 * --autotest runs <frames> paced frames in the window, saves the last one, prints the
 * measured frame rate and exits.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "file_util.h"
#include "gba.h"
#include "png.h"

#define GBA_FPS 59.7275 /* 16777216 cycles/s / 280896 cycles per frame */
#define SCALE 3
#define AUDIO_BUFFERS 8
#define AUDIO_BUFFER_FRAMES 1024
#define AUDIO_QUEUE_TARGET 3 /* buffers (about one frame each) waiting to play */

static gba_t *gba;
static uint16_t held;
static uint32_t pixels[240 * 160]; /* 0x00RRGGBB, top-down */
static int shot_count;

static HWAVEOUT wave_out;
static WAVEHDR wave_hdr[AUDIO_BUFFERS];
static int16_t wave_data[AUDIO_BUFFERS][AUDIO_BUFFER_FRAMES * 2];
static int wave_next;
static char save_path[1024];
static int save_countdown; /* frames until a pending save is written; 0 = nothing pending */
static uint8_t extra_held; /* I, J, K, L, Q, E held: bits 0-5 */
static int light_level;    /* 0..255 */

static uint16_t key_for(WPARAM vk) {
    switch (vk) {
    case VK_UP: return GBA_KEY_UP;
    case VK_DOWN: return GBA_KEY_DOWN;
    case VK_LEFT: return GBA_KEY_LEFT;
    case VK_RIGHT: return GBA_KEY_RIGHT;
    case 'Z': return GBA_KEY_A;
    case 'X': return GBA_KEY_B;
    case 'A': return GBA_KEY_L;
    case 'S': return GBA_KEY_R;
    case VK_RETURN: return GBA_KEY_START;
    case VK_BACK: return GBA_KEY_SELECT;
    default: return 0;
    }
}

static uint32_t expand5(uint32_t c) {
    return c << 3 | c >> 2;
}

static void convert_frame(void) {
    const uint16_t *fb = gba_framebuffer(gba);
    for (int i = 0; i < 240 * 160; i++) {
        uint32_t c = fb[i];
        pixels[i] = expand5(c & 31) << 16 | expand5((c >> 5) & 31) << 8 | expand5((c >> 10) & 31);
    }
}

static void save_screenshot(const char *path) {
    if (png_write_bgr555(path, gba_framebuffer(gba), 240, 160) == 0) printf("saved %s\n", path);
    else fprintf(stderr, "cannot write %s\n", path);
}

/* ---- save file ---- */

/* game.gba → game.sav in the same folder. */
static void set_save_path(const char *rom_path) {
    snprintf(save_path, sizeof save_path, "%s", rom_path);
    char *name = save_path;
    for (char *p = save_path; *p; p++) {
        if (*p == '/' || *p == '\\') name = p + 1;
    }
    char *dot = strrchr(name, '.');
    if (dot) *dot = '\0';
    strncat(save_path, ".sav", sizeof save_path - strlen(save_path) - 1);
}

static void load_save(void) {
    size_t size;
    uint8_t *data = read_file(save_path, &size);
    if (!data) return;
    if (gba_load_saveram(gba, data, size) == GBA_OK) printf("loaded %s\n", save_path);
    else fprintf(stderr, "ignoring %s: wrong size for this game's save chip\n", save_path);
    free(data);
}

/* Written to a temporary file first, then swapped in, so a crash never leaves half a save. */
static void write_save(void) {
    size_t size = gba_saveram_size(gba);
    if (!size) return;
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", save_path);
    if (write_file(tmp, gba_saveram(gba), size) == 0 && MoveFileExA(tmp, save_path, MOVEFILE_REPLACE_EXISTING)) {
        printf("saved %s\n", save_path);
    } else {
        fprintf(stderr, "cannot write %s\n", save_path);
    }
}

/* ---- sound ---- */

static bool audio_open(void) {
    WAVEFORMATEX fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = GBA_AUDIO_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = GBA_AUDIO_RATE * 4;
    memset(wave_hdr, 0, sizeof wave_hdr);
    return waveOutOpen(&wave_out, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL) == MMSYSERR_NOERROR;
}

static bool buffer_busy(const WAVEHDR *h) {
    return (h->dwFlags & WHDR_PREPARED) && !(h->dwFlags & WHDR_DONE);
}

/* Buffers handed to waveOut that have not finished playing. */
static int audio_queued(void) {
    int n = 0;
    for (int i = 0; i < AUDIO_BUFFERS; i++) {
        if (buffer_busy(&wave_hdr[i])) n++;
    }
    return n;
}

/* Queue the samples of the frame just emulated. */
static void audio_submit(void) {
    WAVEHDR *h = &wave_hdr[wave_next];
    while (buffer_busy(h)) Sleep(1);
    if (h->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, h, sizeof *h);
    size_t frames = gba_audio_read(gba, wave_data[wave_next], AUDIO_BUFFER_FRAMES);
    if (frames == 0) return;
    memset(h, 0, sizeof *h);
    h->lpData = (LPSTR)wave_data[wave_next];
    h->dwBufferLength = (DWORD)(frames * 4);
    waveOutPrepareHeader(wave_out, h, sizeof *h);
    waveOutWrite(wave_out, h, sizeof *h);
    wave_next = (wave_next + 1) % AUDIO_BUFFERS;
}

static void audio_close(void) {
    waveOutReset(wave_out);
    for (int i = 0; i < AUDIO_BUFFERS; i++) {
        if (wave_hdr[i].dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(wave_out, &wave_hdr[i], sizeof wave_hdr[i]);
    }
    waveOutClose(wave_out);
}

/* ---- cartridge extras ---- */

static uint8_t extra_bit(WPARAM vk) {
    switch (vk) {
    case 'I': return 1;
    case 'J': return 2;
    case 'K': return 4;
    case 'L': return 8;
    case 'Q': return 16;
    case 'E': return 32;
    default: return 0;
    }
}

static void apply_extras(void) {
    int x = ((extra_held & 8) ? 24000 : 0) - ((extra_held & 2) ? 24000 : 0);
    int y = ((extra_held & 4) ? 24000 : 0) - ((extra_held & 1) ? 24000 : 0);
    int turn = ((extra_held & 32) ? 16000 : 0) - ((extra_held & 16) ? 16000 : 0);
    gba_set_tilt(gba, (int16_t)x, (int16_t)y);
    gba_set_gyro(gba, (int16_t)turn);
}

static void update_title(HWND hwnd) {
    static char shown[128];
    char title[128];
    int n = snprintf(title, sizeof title, "gbaview - %s", gba_rom_title(gba));
    if (n > 0 && (gba_cart_hardware(gba) & GBA_HW_SOLAR)) {
        n += snprintf(title + n, sizeof title - (size_t)n, "  light %d", light_level);
    }
    if (n > 0 && gba_rumble_active(gba)) snprintf(title + n, sizeof title - (size_t)n, "  RUMBLE");
    if (strcmp(title, shown) != 0) {
        SetWindowTextA(hwnd, title);
        snprintf(shown, sizeof shown, "%s", title);
    }
}

/* Seconds to add to UTC for the PC's local time (daylight saving included). */
static int32_t local_utc_offset(void) {
    TIME_ZONE_INFORMATION tz;
    DWORD mode = GetTimeZoneInformation(&tz);
    LONG bias = tz.Bias;
    if (mode == TIME_ZONE_ID_DAYLIGHT) bias += tz.DaylightBias;
    else if (mode == TIME_ZONE_ID_STANDARD) bias += tz.StandardBias;
    return (int32_t)(-bias * 60);
}

/* ---- window ---- */

static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wp == VK_F12) {
            char name[32];
            snprintf(name, sizeof name, "gbaview-%d.png", shot_count++);
            save_screenshot(name);
            return 0;
        }
        if (wp == VK_OEM_4 || wp == VK_OEM_6) { /* [ and ]: darker / brighter */
            light_level += wp == VK_OEM_6 ? 32 : -32;
            if (light_level < 0) light_level = 0;
            if (light_level > 255) light_level = 255;
            gba_set_light(gba, (uint8_t)light_level);
            return 0;
        }
        extra_held |= extra_bit(wp);
        held |= key_for(wp);
        gba_set_keys(gba, held);
        return 0;
    case WM_KEYUP:
        extra_held &= (uint8_t)~extra_bit(wp);
        held &= (uint16_t)~key_for(wp);
        gba_set_keys(gba, held);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof bmi);
        bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
        bmi.bmiHeader.biWidth = 240;
        bmi.bmiHeader.biHeight = -160; /* negative: rows run top to bottom */
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR); /* nearest neighbour: sharp pixels */
        StretchDIBits(dc, 0, 0, rc.right, rc.bottom, 0, 0, 240, 160, pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static double seconds_now(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}

/* Sleep until `target` seconds, finishing with a short spin for accuracy. */
static void wait_until(double target) {
    for (;;) {
        double left = target - seconds_now();
        if (left <= 0) return;
        if (left > 0.002) Sleep((DWORD)((left - 0.001) * 1000));
    }
}

static int usage(void) {
    fprintf(stderr, "usage: gbaview <rom.gba> [--bios <file>] [--mute] [--autotest <frames> <out.png>]\n");
    return 3;
}

int main(int argc, char **argv) {
    const char *rom_path = NULL, *bios_path = NULL, *auto_png = NULL;
    long auto_frames = 0;
    bool mute = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bios") && i + 1 < argc) bios_path = argv[++i];
        else if (!strcmp(argv[i], "--mute")) mute = true;
        else if (!strcmp(argv[i], "--autotest") && i + 2 < argc) {
            auto_frames = strtol(argv[++i], NULL, 0);
            auto_png = argv[++i];
        } else if (argv[i][0] != '-' && !rom_path) rom_path = argv[i];
        else return usage();
    }
    if (!rom_path) return usage();

    size_t rom_size;
    uint8_t *rom = read_file(rom_path, &rom_size);
    if (!rom) {
        fprintf(stderr, "cannot read %s\n", rom_path);
        return 3;
    }
    gba = gba_create(NULL);
    if (bios_path) {
        size_t bios_size;
        uint8_t *bios = read_file(bios_path, &bios_size);
        gba_err_t err = bios ? gba_load_bios(gba, bios, bios_size) : GBA_ERR_BIOS_SIZE;
        free(bios);
        if (err != GBA_OK) {
            fprintf(stderr, "cannot load BIOS %s\n", bios_path);
            return 3;
        }
    }
    if (gba_load_rom(gba, rom, rom_size) != GBA_OK) {
        fprintf(stderr, "cannot load ROM %s\n", rom_path);
        return 3;
    }
    set_save_path(rom_path);
    load_save();
    gba_set_rtc(gba, (int64_t)time(NULL), local_utc_offset());

    WNDCLASSA wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "gbaview";
    RegisterClassA(&wc);
    RECT r = {0, 0, 240 * SCALE, 160 * SCALE};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    char title[64];
    snprintf(title, sizeof title, "gbaview - %s", gba_rom_title(gba));
    HWND hwnd = CreateWindowA("gbaview", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        fprintf(stderr, "cannot create window\n");
        return 3;
    }

    bool sound = !mute && audio_open();
    if (!mute && !sound) fprintf(stderr, "no audio device: running muted\n");
    timeBeginPeriod(1); /* 1 ms Sleep resolution */
    const double frame_time = 1.0 / GBA_FPS;
    double start = seconds_now(), next = start;
    long frames = 0;
    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if (!running) break;

        apply_extras();
        gba_run_frame(gba);
        if (gba_saveram_dirty(gba)) save_countdown = 60; /* write once the game has stopped saving */
        else if (save_countdown > 0 && --save_countdown == 0) write_save();
        convert_frame();
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateWindow(hwnd);
        update_title(hwnd);
        frames++;

        if (auto_frames > 0 && frames >= auto_frames) {
            double elapsed = seconds_now() - start;
            printf("%ld frames in %.2f s (%.1f fps)\n", frames, elapsed, (double)frames / elapsed);
            save_screenshot(auto_png);
            DestroyWindow(hwnd);
            break;
        }
        if (sound) {
            audio_submit();
            while (audio_queued() >= AUDIO_QUEUE_TARGET) Sleep(1); /* the sound card sets the pace */
        } else {
            static int16_t discard[2048 * 2];
            gba_audio_read(gba, discard, 2048);
            next += frame_time;
            double now = seconds_now();
            if (now - next > 0.25) next = now; /* far behind (e.g. window dragged): resync instead of racing */
            wait_until(next);
        }
    }
    if (save_countdown > 0) write_save();
    if (sound) audio_close();
    timeEndPeriod(1);
    gba_destroy(gba);
    free(rom);
    return 0;
}
