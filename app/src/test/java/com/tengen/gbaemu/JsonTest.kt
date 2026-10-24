package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class JsonTest {
    @Test
    fun decimalsReadWithFractionsAndExponents() {
        assertEquals(47.5, Json.Reader("47.5").decimal(), 0.0)
        assertEquals(-3.0, Json.Reader(" -3").decimal(), 0.0)
        assertEquals(100.0, Json.Reader("1e2").decimal(), 0.0)
        assertEquals(0.25, Json.Reader("2.5E-1").decimal(), 0.0)
    }

    @Test
    fun aValueIsReadAsPlainKotlin() {
        val v = Json.Reader("""{"a": [1, 2.5, "x\"y", true, false, null], "b": {"c": {}}, "d": []}""").run { value().also { end() } }
        assertEquals(mapOf("a" to listOf(1.0, 2.5, "x\"y", true, false, null), "b" to mapOf("c" to emptyMap<String, Any?>()),
            "d" to emptyList<Any?>()), v)
        assertEquals(listOf("a", "b", "d"), (v as Map<*, *>).keys.toList()) // in the file's order
    }

    @Test
    fun brokenJsonThrows() {
        for (bad in listOf("{", "[1,", "{\"a\" 1}", "-", "nul", "\"open")) {
            assertThrows(bad, IllegalStateException::class.java) { Json.Reader(bad).value() }
        }
    }
}
