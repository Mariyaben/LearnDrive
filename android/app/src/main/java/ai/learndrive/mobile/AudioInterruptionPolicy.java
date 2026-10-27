package ai.learndrive.mobile;

/** Native-validated WARN authority is scoped to one coaching ticket. Legacy
 * phrase priority still governs competing warnings and ordinary submissions. */
public final class AudioInterruptionPolicy {
    private AudioInterruptionPolicy() {}
    public static boolean mayReplace(boolean validatedWarning, boolean currentWarning,
                                     int currentPhrasePriority, boolean legacyAllowed) {
        if (currentWarning && !validatedWarning) return false;
        if (legacyAllowed) return true;
        return validatedWarning && !currentWarning && currentPhrasePriority <= 2;
    }
}
