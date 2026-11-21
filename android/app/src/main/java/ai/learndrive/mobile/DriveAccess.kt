package ai.learndrive.mobile

/**
 * Whether the learner driving may start a drive, from what the subscription check last confirmed.
 * Asked only when a drive is about to start, parked. A drive under way is never stopped for a
 * payment, a lapsed subscription or a lost signal: the coach going quiet mid-drive could read as an
 * all clear. Not required (BuildConfig.SUBSCRIPTION_REQUIRED) until Google Play billing is in the app.
 */
object DriveAccess {
    /** What the subscription server last confirmed: paid up to [until], checked at [checkedAt] (wall-clock ms). */
    data class Paid(val until:Long,val checkedAt:Long)
    enum class Decision(val allowed:Boolean) {
        NOT_REQUIRED(true),PAID(true),
        /** The paid time has run out since the last check, so a renewal could not be confirmed yet (no signal). */
        RENEWAL_UNCONFIRMED(true),
        NOT_SUBSCRIBED(false),LAPSED(false)
    }
    /** How long starting stays allowed while a renewal is due but not yet confirmed. */
    const val RENEWAL_GRACE_MS=3L*24*60*60*1000

    /** [latestSeen]: the latest wall-clock time this phone has shown the app, so that setting the
     * clock back restores nothing. */
    fun decide(required:Boolean,now:Long,latestSeen:Long,paid:Paid?):Decision {
        if(!required)return Decision.NOT_REQUIRED
        if(paid==null)return Decision.NOT_SUBSCRIBED
        val t=maxOf(now,latestSeen,paid.checkedAt)
        return when {
            t<paid.until->Decision.PAID
            // Confirmed after it ran out and still not paid: no grace.
            paid.checkedAt<paid.until&&t-paid.until<RENEWAL_GRACE_MS->Decision.RENEWAL_UNCONFIRMED
            else->Decision.LAPSED
        }
    }
}
