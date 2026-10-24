package com.tengen.gbaemu

import java.nio.ByteBuffer

/** The native side of RetroAchievements (NativeBridge's ra* functions) that the controller uses, as an interface for tests. */
interface RaNative {
    fun loginPassword(user: String, password: String)
    fun loginToken(user: String, token: String)
    fun logout()
    fun loginStatus(): IntArray // [status, code]
    fun loginMessage(): String
    fun user(): Array<String>? // [name, token]
    fun loadGame(rom: ByteBuffer)
    fun unloadGame()
    fun poll()
    fun gameStatus(): IntArray // [status, code]
    fun summary(): IntArray? // [unlocked, total, points unlocked, points total]
}

/** The real one. */
object NativeRa : RaNative {
    private fun bytes(s: String) = s.toByteArray(Charsets.UTF_8)
    override fun loginPassword(user: String, password: String) = NativeBridge.raLoginPassword(bytes(user), bytes(password))
    override fun loginToken(user: String, token: String) = NativeBridge.raLoginToken(bytes(user), bytes(token))
    override fun logout() = NativeBridge.raLogout()
    override fun loginStatus() = NativeBridge.raLoginStatus()
    override fun loginMessage() = String(NativeBridge.raLoginMessage(), Charsets.UTF_8)
    override fun user() = NativeBridge.raUser()?.map { String(it, Charsets.UTF_8) }?.toTypedArray()
    override fun loadGame(rom: ByteBuffer) = NativeBridge.raLoadGame(rom)
    override fun unloadGame() = NativeBridge.raUnloadGame()
    override fun poll() = NativeBridge.raPoll()
    override fun gameStatus() = NativeBridge.raGameStatus()
    override fun summary() = NativeBridge.raSummary()
}

/**
 * Signing in to RetroAchievements and identifying games (14). The password goes to rcheevos once; the token it gives
 * back is kept in [account]. A saved sign-in that the server refuses becomes [SignIn.AGAIN] ("Sign in again"); one that
 * can't reach the server stays [SignIn.IN] and is tried again with the next game.
 */
class Achievements(private val native: RaNative, private val account: RaAccount) {
    enum class SignIn { OUT, BUSY, IN, AGAIN }

    sealed interface Game {
        data object NoSet : Game
        data object Offline : Game
        data class Set(val unlocked: Int, val total: Int) : Game
    }

    var signIn = SignIn.OUT
        private set
    var name: String? = null
        private set
    var error: String? = null // the last failed password sign-in's reason
        private set
    private var byToken = false

    /** App start: the saved sign-in, if there is one. */
    fun start() {
        val (user, token) = account.load() ?: return
        name = user
        byToken = true
        signIn = SignIn.BUSY
        native.loginToken(user, token)
    }

    fun signIn(user: String, password: String) {
        error = null
        name = user
        byToken = false
        signIn = SignIn.BUSY
        native.loginPassword(user, password)
    }

    fun signOut() {
        native.logout()
        account.clear()
        name = null
        error = null
        signIn = SignIn.OUT
    }

    /** Picks up a finished sign-in; true when [signIn] changed. Call it often while [signIn] is BUSY. */
    fun update(): Boolean {
        if (signIn != SignIn.BUSY) return false
        val st = native.loginStatus()
        when (st[0]) {
            NativeBridge.RA_OK -> {
                native.user()?.let { (user, token) ->
                    name = user
                    if (!account.save(user, token)) error = "Couldn't keep the sign-in on this phone."
                }
                signIn = SignIn.IN
            }
            NativeBridge.RA_FAILED -> if (byToken) {
                if (st[1] in CREDENTIAL_ERRORS) {
                    account.clear()
                    signIn = SignIn.AGAIN
                } else {
                    signIn = SignIn.IN // no internet: kept, tried again with the next game
                }
            } else {
                error = native.loginMessage().ifEmpty { "Couldn't sign in." }
                signIn = SignIn.OUT
            }
            else -> return false
        }
        return true
    }

    /** A game was loaded ([rom]): starts identifying it when signed in; false when there is nothing to wait for. */
    fun loadGame(rom: ByteBuffer): Boolean {
        if (signIn != SignIn.IN && signIn != SignIn.BUSY) return false
        if (signIn == SignIn.IN && native.loginStatus()[0] != NativeBridge.RA_OK) {
            account.load()?.let { (user, token) ->
                byToken = true
                signIn = SignIn.BUSY
                native.loginToken(user, token)
            }
        }
        native.loadGame(rom)
        return true
    }

    /** One step of waiting for [loadGame] (every ~50 ms): null while RetroAchievements is still answering; [timedOut] gives up. */
    fun gameStep(timedOut: Boolean): Game? {
        native.poll()
        update()
        val st = native.gameStatus()
        return when (st[0]) {
            NativeBridge.RA_BUSY -> if (timedOut) Game.Offline.also { native.unloadGame() } else null
            NativeBridge.RA_OK -> native.summary()?.takeIf { it[1] > 0 }?.let { Game.Set(it[0], it[1]) } ?: Game.NoSet
            NativeBridge.RA_FAILED ->
                if (st[1] == RC_NO_GAME_LOADED || signIn == SignIn.AGAIN || signIn == SignIn.OUT) Game.NoSet else Game.Offline
            else -> Game.NoSet
        }
    }

    companion object {
        // rcheevos' codes (third_party/rcheevos/include/rc_error.h)
        const val RC_LOGIN_REQUIRED = -28
        const val RC_NO_GAME_LOADED = -29
        const val RC_ACCESS_DENIED = -33
        const val RC_INVALID_CREDENTIALS = -34
        const val RC_EXPIRED_TOKEN = -35
        val CREDENTIAL_ERRORS = setOf(RC_ACCESS_DENIED, RC_INVALID_CREDENTIALS, RC_EXPIRED_TOKEN)
    }
}
