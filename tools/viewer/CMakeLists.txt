add_executable(gbaview main.c)
target_link_libraries(gbaview PRIVATE gbacore gbatools gdi32 winmm)
target_compile_options(gbaview PRIVATE -Wall -Wextra)
