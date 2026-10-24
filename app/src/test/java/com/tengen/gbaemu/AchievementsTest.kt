package com.tengen.gbaemu

import java.io.File
import java.nio.ByteBuffer
import java.nio.file.Files
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class AchievementsTest {
    /** The native side as a script: each call is noted; statuses are set by the test. */
    private class FakeNative : RaNative {
        var login = intArrayOf(NativeBridge.RA_NOTHING, 0)
        var message = ""
        var userInfo: Array<String>? = null
        var game = intArrayOf(NativeBridge.RA_NOTHING, 0)
        var summaryInfo: IntArray? = null
        val calls = mutableListOf<String>()
        override fun loginPassword(user: String, password: String) { calls += "password $user"; login = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun loginToken(user: String, token: String) { calls += "token $user $token"; login = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun logout() { calls += "logout"; login = intArrayOf(NativeBridge.RA_NOTHING, 0) }
        override fun loginStatus() = login
        override fun loginMessage() = message
        override fun user() = userInfo
        override fun loadGame(rom: ByteBuffer) { calls += "load"; game = intArrayOf(NativeBridge.RA_BUSY, 0) }
        override fun unloadGame() { calls += "unload"; game = intArrayOf(NativeBridge.RA_NOTHING, 0) }
        override fun poll() { calls += "poll" }
        override fun gameStatus() = game
        override fun summary() = summaryInfo
    }

    private val dir: File = Files.createTempDirectory("ach").toFile()
    private val account = RaAccount(File(dir, "ra.json"))
    private val native = FakeNative()
    private val rom: ByteBuffer = ByteBuffer.allocateDirect(4)

    private fun signedIn(): Achievements {
        val a = Achievements(native, account)
        a.signIn("Tester", "secret")
        native.login = intArrayOf(NativeBridge.RA_OK, 0)
        native.userInfo = arrayOf("Tester", "TOKEN")
        assertTrue(a.update())
        return a
    }

    @Test
    fun aPasswordSignInKeepsTheTokenNotThePassword() {
        val a = signedIn()
        assertEquals(Achievements.SignIn.IN, a.signIn)
        assertEquals("Tester", a.name)
        val kept = File(dir, "ra.json").readText()
        assertTrue(kept, "TOKEN" in kept)
        assertFalse(kept, "secret" in kept)
        assertFalse(a.update()) // nothing new
    }

    @Test
    fun aWrongPasswordShowsTheServersMessageAndKeepsNothing() {
        val a = Achievements(native, account)
        a.signIn("Tester", "nope")
        assertEquals(Achievements.SignIn.BUSY, a.signIn)
        assertFalse(a.update()) // still busy
        native.login = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_INVALID_CREDENTIALS)
        native.message = "Invalid User/Password combination. Please try again."
        assertTrue(a.update())
        assertEquals(Achievements.SignIn.OUT, a.signIn)
        assertEquals("Invalid User/Password combination. Please try again.", a.error)
        assertNull(account.load())
    }

    @Test
    fun theSavedSignInIsUsedAtStart() {
        account.save("Tester", "TOKEN")
        val a = Achievements(native, account)
        a.start()
        assertEquals(listOf("token Tester TOKEN"), native.calls)
        assertEquals(Achievements.SignIn.BUSY, a.signIn)
        native.login = intArrayOf(NativeBridge.RA_OK, 0)
        native.userInfo = arrayOf("Tester", "TOKEN")
        a.update()
        assertEquals(Achievements.SignIn.IN, a.signIn)
    }

    @Test
    fun anExpiredTokenAsksToSignInAgain() {
        account.save("Tester", "OLD")
        val a = Achievements(native, account)
        a.start()
        native.login = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_EXPIRED_TOKEN)
        a.update()
        assertEquals(Achievements.SignIn.AGAIN, a.signIn)
        assertNull(account.load())
        assertFalse(a.loadGame(rom)) // no game is identified until the user signs in again
    }

    @Test
    fun noInternetAtStartKeepsTheSignInAndTriesAgainWithTheNextGame() {
        account.save("Tester", "TOKEN")
        val a = Achievements(native, account)
        a.start()
        native.login = intArrayOf(NativeBridge.RA_FAILED, -32) // RC_NO_RESPONSE
        a.update()
        assertEquals(Achievements.SignIn.IN, a.signIn)
        assertEquals("Tester" to "TOKEN", account.load())
        assertTrue(a.loadGame(rom))
        assertEquals(listOf("token Tester TOKEN", "token Tester TOKEN", "load"), native.calls)
    }

    @Test
    fun signOutForgetsEverything() {
        val a = signedIn()
        a.signOut()
        assertEquals(Achievements.SignIn.OUT, a.signIn)
        assertNull(a.name)
        assertNull(account.load())
        assertTrue("logout" in native.calls)
    }

    @Test
    fun aGameWithASetSaysHowManyAreUnlocked() {
        val a = signedIn()
        assertTrue(a.loadGame(rom))
        assertNull(a.gameStep(timedOut = false))
        assertTrue("poll" in native.calls)
        native.game = intArrayOf(NativeBridge.RA_OK, 0)
        native.summaryInfo = intArrayOf(1, 5, 5, 43)
        assertEquals(Achievements.Game.Set(1, 5), a.gameStep(timedOut = false))
    }

    @Test
    fun anUnknownGameHasNoSetAndAFailedLoadIsOffline() {
        val a = signedIn()
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_FAILED, Achievements.RC_NO_GAME_LOADED)
        assertEquals(Achievements.Game.NoSet, a.gameStep(timedOut = false))
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_FAILED, -32)
        assertEquals(Achievements.Game.Offline, a.gameStep(timedOut = false))
        a.loadGame(rom)
        native.game = intArrayOf(NativeBridge.RA_OK, 0)
        native.summaryInfo = null // loaded, but no achievements
        assertEquals(Achievements.Game.NoSet, a.gameStep(timedOut = false))
    }

    @Test
    fun noAnswerInTimeUnloadsAndSaysOffline() {
        val a = signedIn()
        a.loadGame(rom)
        assertEquals(Achievements.Game.Offline, a.gameStep(timedOut = true))
        assertTrue("unload" in native.calls)
    }

    @Test
    fun signedOutIdentifiesNothing() {
        assertFalse(Achievements(native, account).loadGame(rom))
        assertFalse("load" in native.calls)
    }
}
