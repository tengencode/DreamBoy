package com.tengen.gbaemu

/**
 * libretro's list of GBA games (10b): each ROM's fingerprint (CRC32, as [GameId] writes it), its 4-letter game code
 * and the game's name — the name box art and cheat files are filed under. Read from the No-Intro `.dat`, kept as
 * tab-separated lines. Pure: tested on the PC.
 */
class GameNames(val rows: List<Row>) {
    /** One ROM: its fingerprint (8 lowercase hex digits), its code ("" if none) and its game's name. */
    data class Row(val id: String, val code: String, val name: String)

    private val byId = rows.associate { it.id to it.name }
    private val byCode = rows.filter { validCode(it.code) }.groupBy { it.code }
        .mapValues { (_, same) -> same.map { it.name }.minWith(compareBy<String> { it.length }.thenBy { it }) }

    /** The game's name: by its fingerprint, else by its header code (the plainest name of those sharing it). */
    fun find(id: String, code: String): String? = byId[id.lowercase()] ?: if (validCode(code)) byCode[code] else null

    fun toTsv(): String = rows.joinToString("") { "${it.id}\t${it.code}\t${it.name}\n" }

    companion object {
        private val CODE = Regex("[A-Z0-9]{4}")
        private val CRC = Regex("""\bcrc ([0-9A-Fa-f]{8})\b""")
        private val SERIAL = Regex("\\bserial \"([^\"]*)\"")
        private val NAME = Regex("name \"([^\"]*)\"")

        /** The characters libretro replaces with `_` in picture file names. */
        private val REPLACED = setOf('&', '*', '/', ':', '`', '<', '>', '?', '\\', '|')

        fun validCode(code: String) = CODE.matches(code)

        /** A clrmamepro `.dat`: per `game ( … )` its name, and per `rom ( … )` line its crc and serial. */
        fun fromDat(text: String): GameNames {
            val rows = mutableListOf<Row>()
            var inGame = false
            var name: String? = null
            var serial = ""
            for (raw in text.lines()) {
                val line = raw.trim()
                if (line == "game (") {
                    inGame = true
                    name = null
                    serial = ""
                    continue
                }
                if (!inGame) continue
                when {
                    line == ")" -> inGame = false
                    line.startsWith("name \"") && name == null -> name = NAME.find(line)?.groupValues?.get(1)
                    line.startsWith("serial \"") -> serial = SERIAL.find(line)?.groupValues?.get(1) ?: ""
                    line.startsWith("rom (") -> {
                        val rest = line.replaceFirst(NAME, "") // the ROM's file name could hold anything
                        val crc = CRC.find(rest)?.groupValues?.get(1)?.lowercase()
                        val game = name
                        if (crc != null && game != null) rows += Row(crc, SERIAL.find(rest)?.groupValues?.get(1) ?: serial, game)
                    }
                }
            }
            return GameNames(rows)
        }

        /** What [toTsv] writes; lines starting with `#` and broken lines are skipped. */
        fun fromTsv(text: String): GameNames = GameNames(text.lines().mapNotNull { line ->
            val parts = line.split('\t')
            if (line.startsWith("#") || parts.size != 3 || parts[0].length != 8) null else Row(parts[0], parts[1], parts[2])
        })

        /** The ROM header's game code (4 bytes at 0xAC) if it is 4 capitals or digits, else "". */
        fun code(rom: ByteArray): String {
            if (rom.size < 0xB0) return ""
            return String(rom, 0xAC, 4, Charsets.ISO_8859_1).takeIf(::validCode) ?: ""
        }

        /** The game's name as libretro names its pictures (without `.png`). */
        fun pictureName(name: String): String = name.map { if (it in REPLACED) '_' else it }.joinToString("")
    }
}
