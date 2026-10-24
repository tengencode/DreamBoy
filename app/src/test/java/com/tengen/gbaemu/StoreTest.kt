package com.tengen.gbaemu

import org.junit.Assert.assertEquals
import org.junit.Test

class StoreTest {
    @Test
    fun aMemoryStoreGivesEveryValueAndEmpties() {
        val s = MemoryStore()
        s.put("a", 1)
        s.put("b", 2)
        s.put("a", null)
        s.put("c", 3)
        assertEquals(mapOf("b" to 2, "c" to 3), s.all())
        s.clear()
        assertEquals(emptyMap<String, Int>(), s.all())
        assertEquals(null, s.get("b"))
    }
}
