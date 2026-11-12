package ai.learndrive.mobile;

/**
 * "You first" in words. The numbers come from the native record (ld_learner_summary): this
 * drive's outcomes (slowed before being told, after being told, no slowing seen), the wait level
 * per kind of situation, and how many recent outcomes are kept per kind. Measured speed only:
 * an observation of slowing, never a grade of driving.
 */
public final class LearnerReview {
    private LearnerReview() {}

    private static final String[] KINDS = {"people ahead", "two-wheelers ahead", "vehicles ahead", "oncoming traffic"};
    private static final String[] SEEN = {"Person ahead", "Two-wheeler ahead", "Vehicle ahead", "Oncoming traffic"};

    private static boolean valid(int[] s) { return s != null && s.length >= 11; }

    /** The lines added to the review of the drive just finished. */
    public static String drive(int[] s) {
        StringBuilder out = new StringBuilder("Road users and your speed");
        if (!valid(s) || s[0] + s[1] + s[2] == 0)
            return out.append("\nNo road-user situations were measured on this drive.").toString();
        int total = s[0] + s[1] + s[2];
        out.append("\nYou slowed before being told: ").append(s[0]).append(" of ").append(total);
        out.append("\nYou slowed after being told: ").append(s[1]);
        out.append("\nNo slowing seen: ").append(s[2]);
        out.append("\n").append(levels(s));
        out.append("\nMeasured from your speed only. The app cannot see where you looked, why you slowed or what your feet did: this is an observation, not a driving grade.");
        return out.toString();
    }

    /** What the coach has learned so far, for the practice records. */
    public static String status(int[] s) {
        if (!valid(s)) return "";
        int kept = s[7] + s[8] + s[9] + s[10];
        if (kept == 0) return "The coach has no record yet of how you respond to road users.";
        return "Road-user situations on record: " + kept + ". " + levels(s);
    }

    private static String levels(int[] s) {
        String shorter = list(s, 1), quiet = list(s, 2);
        if (shorter.isEmpty() && quiet.isEmpty())
            return "The coach still tells you everything in full. Once your record over several drives shows that you slow for road users, it leaves out what your speed shows you have already done.";
        StringBuilder out = new StringBuilder();
        if (!shorter.isEmpty()) out.append("Once you have slowed, the coach now leaves the slowing out of its instruction for: ").append(shorter).append(".");
        if (!quiet.isEmpty()) out.append(out.length() > 0 ? " " : "").append("Once you have slowed, the coach now stays quiet and shows on screen what it has seen for: ").append(quiet).append(".");
        return out.toString();
    }

    private static String list(int[] s, int level) {
        StringBuilder out = new StringBuilder();
        for (int k = 0; k < 4; k++) if (s[3 + k] == level) out.append(out.length() > 0 ? ", " : "").append(KINDS[k]);
        return out.toString();
    }

    /** Shown while an instruction is left unsaid: the coach has seen the road user. */
    public static String holdCaption(int kind) {
        String seen = kind >= 0 && kind < SEEN.length ? SEEN[kind] : "Road user ahead";
        return seen + " · seen. You slowed first.";
    }
}
