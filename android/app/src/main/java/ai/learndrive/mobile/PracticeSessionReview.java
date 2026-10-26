package ai.learndrive.mobile;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;

/** Bounded app-lifetime cue history, not an assessment of the learner's actions. */
public final class PracticeSessionReview {
    public enum Access { UNAVAILABLE, CONFIRM_PARKED, READY }

    /** One policy for opening and rechecking a review; an unknown speed cannot prove parking. */
    public static Access access(boolean monitoring, boolean moving, boolean hasReview, boolean measuredStopped) {
        if (monitoring || moving || !hasReview) return Access.UNAVAILABLE;
        return measuredStopped ? Access.READY : Access.CONFIRM_PARKED;
    }
    private static final int MAX_PHRASES = 12;
    private static final double MAX_SAMPLE_GAP = .5;
    private boolean active, previousCurrent;
    private double started, lastSample, lastAccepted, checkedSeconds, currentSeconds;
    private int roadCues, navigationCues, voiceSubmissions;
    private final LinkedHashMap<String, Cue> cues = new LinkedHashMap<>();

    public static final class Cue {
        public final int origin, count;
        public final String phrase;
        private Cue(int origin, String phrase, int count) {
            this.origin = origin; this.phrase = phrase; this.count = count;
        }
    }

    public static final class Review {
        public final double durationSeconds, checkedSeconds, currentSeconds, unmeasuredSeconds;
        public final int roadCues, navigationCues, voiceSubmissions;
        public final List<Cue> cues;
        private Review(double duration, double checked, double current, int road, int navigation, int voice, List<Cue> cues) {
            durationSeconds = duration;
            checkedSeconds = Math.min(duration, checked);
            currentSeconds = Math.min(checkedSeconds, current);
            unmeasuredSeconds = Math.max(0, duration - checkedSeconds);
            roadCues = road; navigationCues = navigation; voiceSubmissions = voice;
            this.cues = Collections.unmodifiableList(cues);
        }
        public String text() {
            StringBuilder out = new StringBuilder("Duration: ").append(duration(durationSeconds));
            out.append("\n\nEstimated camera availability: ");
            if (durationSeconds < .1) out.append("not enough session time.");
            else out.append((int)Math.floor(100 * currentSeconds / durationSeconds + 1e-9))
                .append("%")
                .append("\nBased on repeated checks; unmeasured time is not counted as available.")
                .append("\nUnmeasured time: ").append(duration(unmeasuredSeconds));
            out.append("\n\nCoaching cues: ").append(roadCues + navigationCues)
                .append("\nRoad: ").append(roadCues).append(" · Navigation: ").append(navigationCues);
            out.append("\nVoice submissions: ").append(voiceSubmissions)
                .append(". Playback and hearing are not confirmed.");
            if (roadCues + navigationCues == 0)
                out.append("\nNo cues does not mean there were no hazards.");
            int shown = 0, shownCount = 0;
            for (Cue cue : cues) {
                if (shown++ == 6) break;
                out.append("\n• ").append(cue.phrase).append(" ×").append(cue.count);
                shownCount += cue.count;
            }
            int other = roadCues + navigationCues - shownCount;
            if (other > 0) out.append("\n• Other cues: ").append(other);
            out.append("\n\nDiscuss with your instructor");
            boolean roadUsers = false, bend = false, inclination = false, vehicle = false;
            for (Cue cue : cues) {
                String phrase = cue.phrase.toLowerCase(Locale.ROOT);
                roadUsers |= cue.origin != 5 && (phrase.contains("pedestrian") || phrase.contains("cyclist") ||
                    phrase.contains("space") || phrase.contains("gap") || phrase.contains("traffic") || phrase.contains("vehicle"));
                bend |= (cue.origin == 3 || cue.origin == 4 || cue.origin == 5) &&
                    (phrase.contains("bend") || phrase.contains("curve") || phrase.contains("turn") || phrase.contains("roundabout"));
                inclination |= cue.origin == 3 && (phrase.contains("uphill") || phrase.contains("downhill") || phrase.contains("slope"));
                vehicle |= cue.origin != 5 && (phrase.contains("gear") || phrase.contains("clutch") || phrase.contains("engine revs"));
            }
            if (roadUsers) out.append("\n• Space cues: which road users needed attention, and was the cue early enough?");
            if (bend) out.append("\n• Turn/bend cues: when did you look ahead and prepare your speed?");
            if (inclination) out.append("\n• Inclination cues: did they match the road? Phone inclination is an estimate.");
            if (vehicle) out.append("\n• Vehicle cues: discuss suitable control choices. Current gear was not measured.");
            if (!roadUsers && !bend && !inclination && !vehicle)
                out.append("\n• Choose one situation from the drive to practise next.");
            out.append("\n• Which cues were useful, missed or distracting?");
            out.append("\n\nAvailability means freshness, not accuracy. Cues do not establish driving skill or safety.");
            out.append("\nLast completed session in this app session only.");
            return out.toString();
        }
    }

    public synchronized boolean start(double time) {
        if (active || !validTime(time)) return false;
        active = true; started = lastSample = lastAccepted = time;
        checkedSeconds = currentSeconds = 0; roadCues = navigationCues = voiceSubmissions = 0;
        previousCurrent = false; cues.clear();
        return true;
    }

    /** Both ends must be current; missed polling intervals are never credited as available. */
    public synchronized void sample(double time, boolean current) {
        if (!active || !validTime(time) || time <= lastSample) return;
        double elapsed = time - lastSample;
        if (elapsed <= MAX_SAMPLE_GAP) {
            checkedSeconds += elapsed;
            if (previousCurrent && current) currentSeconds += elapsed;
        }
        lastSample = time; previousCurrent = current;
    }

    /** Compatibility for existing accepted voice submissions. Test Voice is excluded. */
    public synchronized boolean accepted(double time, int origin, String phrase) {
        return accepted(time, origin, phrase, true);
    }

    /** Logical road coaching acceptance counts even when optional voice is off. */
    public synchronized boolean accepted(double time, int origin, String phrase, boolean voiceSubmitted) {
        if (!active || !validTime(time) || time < lastAccepted || origin < 1 || origin > 5 ||
            phrase == null || phrase.trim().isEmpty()) return false;
        lastAccepted = time;
        if (origin == 5) navigationCues++; else roadCues++;
        if (voiceSubmitted) voiceSubmissions++;
        String bounded = phrase.trim();
        if (bounded.length() > 160) bounded = bounded.substring(0, 160);
        String key = origin + ":" + bounded;
        Cue prior = cues.get(key);
        if (prior != null) cues.put(key, new Cue(origin, bounded, prior.count + 1));
        else if (cues.size() < MAX_PHRASES) cues.put(key, new Cue(origin, bounded, 1));
        return true;
    }

    public synchronized Review finish(double time) {
        if (!active || !validTime(time) || time < Math.max(lastSample, lastAccepted)) return null;
        active = false;
        return new Review(time - started, checkedSeconds, currentSeconds, roadCues, navigationCues, voiceSubmissions,
            new ArrayList<>(cues.values()));
    }

    /** Backgrounding/source changes stop collection and never masquerade as a completed road lesson. */
    public synchronized void discard() { active = false; cues.clear(); }

    private static boolean validTime(double time) { return Double.isFinite(time) && time >= 0; }
    private static String duration(double seconds) {
        if (seconds > 0 && seconds < 1) return "less than 1 sec";
        long total = (long)Math.max(0, Math.floor(seconds));
        return total >= 60 ? (total / 60) + " min " + (total % 60) + " sec" : total + " sec";
    }
}
