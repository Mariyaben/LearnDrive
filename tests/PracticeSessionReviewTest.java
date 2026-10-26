package ai.learndrive.mobile;

public final class PracticeSessionReviewTest {
    private static int passed;
    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private static void near(double expected, double actual, String message) {
        check(Math.abs(expected - actual) < 1e-8, message + ": " + actual);
    }
    private static void test(String name, Runnable run) {
        run.run(); passed++; System.out.println("PASS " + name);
    }
    public static void main(String[] args) {
        test("visual coaching remains distinct from voice submission", () -> {
            PracticeSessionReview session=new PracticeSessionReview();session.start(0);
            session.accepted(1,4,"Give space",false);session.accepted(2,5,"Prepare for a left turn",true);
            PracticeSessionReview.Review review=session.finish(3);
            check(review.roadCues==1&&review.navigationCues==1,"muted logical coaching disappeared");
            check(review.voiceSubmissions==1,"visual cue invented an audio submission");
            check(review.text().contains("Voice submissions: 1")&&review.text().contains("hearing are not confirmed"),"review implies heard speech");
        });
        test("terrain stage cannot turn a bend cue into a measured inclination or vehicle lesson", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(1);
            session.accepted(2, 3, "Right bend ahead. Ease off early and stay in your lane");
            String text = session.finish(3).text();
            check(text.contains("Turn/bend cues:"), "Camera-derived bend has no learning topic");
            check(!text.contains("Inclination cues:") && !text.contains("Vehicle cues:"), "Stage invented sensor provenance or a gear lesson");
        });
        test("inclination and gear discussion require their actual delivered phrase topics", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(1);
            session.accepted(2, 3, "Steep downhill. Suitable low gear");
            String text = session.finish(3).text();
            check(text.contains("Inclination cues:") && text.contains("Vehicle cues:"), "Delivered topics lost");
        });
        test("only real admitted sessions and supported accepted cue origins count", () -> {
            PracticeSessionReview session = new PracticeSessionReview();
            check(!session.accepted(1, 4, "Give space"), "setup/Test Voice entered the road review");
            check(session.finish(1) == null, "a session appeared without admission");
            check(!session.start(Double.NaN) && !session.start(-1), "invalid session start");
            check(session.start(10), "valid admission rejected");
            check(!session.accepted(10.1, 0, "Test voice") && !session.accepted(10.1, 6, "Unknown"), "unsupported origin counted");
            check(!session.accepted(10.1, 4, " ") && !session.accepted(10.1, 4, null), "empty phrase counted");
            check(session.accepted(10.1, 4, "Give the pedestrian space"), "accepted road cue lost");
            check(session.finish(11).roadCues == 1, "road cue total differs from accepted cues");
        });
        test("a failed repeat admission cannot reset history; a new admitted session can", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(10);
            session.accepted(11, 4, "Give space");
            check(!session.start(12), "ongoing admission reset its existing history");
            PracticeSessionReview.Review first = session.finish(13);
            check(first.roadCues == 1 && first.durationSeconds == 3, "failed admission altered review");
            check(session.start(20), "new admitted session could not start");
            PracticeSessionReview.Review second = session.finish(21);
            check(second.roadCues == 0 && second.navigationCues == 0 && second.cues.isEmpty(), "prior cues leaked to new session");
            check(first.roadCues == 1 && first.cues.size() == 1, "new session mutated an existing snapshot");
        });
        test("sparse polling leaves missing time unmeasured in the full duration denominator", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            session.sample(.1, true);session.sample(.2, true);session.sample(9.9, true);
            PracticeSessionReview.Review review = session.finish(10);
            near(10, review.durationSeconds, "session duration shortened to observed samples");
            near(.2, review.checkedSeconds, "long polling stall credited as checked time");
            near(.1, review.currentSeconds, "long polling stall credited as available");
            near(9.8, review.unmeasuredSeconds, "unobserved tail or gap hidden");
            check(!review.text().contains("100%") && review.text().contains("Estimated camera availability: 1%"), "sparse sampling claims continuous availability");
            check(review.text().contains("Based on repeated checks") && review.text().contains("unmeasured time is not counted as available"), "sampling estimate is presented as continuous measurement");
        });
        test("availability requires current evidence at both sampled endpoints", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            session.sample(.1, false);session.sample(.2, true);session.sample(.3, true);
            session.sample(.4, false);session.sample(.5, false);
            PracticeSessionReview.Review review = session.finish(.5);
            near(.5, review.checkedSeconds, "regular status samples disappeared");
            near(.1, review.currentSeconds, "stale transitions counted as current");
            near(0, review.unmeasuredSeconds, "continuous checks look unmeasured");
            check(review.text().contains("Estimated camera availability: 20%"), "availability has the wrong denominator");
        });
        test("corrupt and reversed times cannot inflate duration or cue history", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(10);
            session.sample(10.1, true);
            for (double invalid : new double[]{Double.NaN, Double.POSITIVE_INFINITY, -1, 9}) {
                session.sample(invalid, true);
                check(!session.accepted(invalid, 4, "Give space"), "corrupt cue time accepted");
                check(session.finish(invalid) == null, "invalid finish accepted");
            }
            session.sample(10.05, true);
            session.accepted(10.2, 4, "Give space");
            check(!session.accepted(10.15, 4, "Give space"), "out-of-order cue accepted twice");
            PracticeSessionReview.Review review = session.finish(11);
            near(1, review.durationSeconds, "invalid times altered start");
            check(review.roadCues == 1, "invalid times altered accepted count");
        });
        test("navigation remains separate and review never labels a cue as a learner fault", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            session.accepted(1, 5, "Prepare for a left turn");
            session.accepted(2, 4, "Give the pedestrian space");
            PracticeSessionReview.Review review = session.finish(3);
            check(review.roadCues == 1 && review.navigationCues == 1, "navigation counted as a road skill fault");
            String text = review.text();
            check(text.contains("Road: 1 · Navigation: 1") && text.contains("Coaching cues: 2"), "cue categories hidden");
            check(text.contains("Prepare for a left turn ×1") && text.contains("Give the pedestrian space ×1"), "takeaways lack actual cue evidence");
            check(text.contains("Space cues:") && text.contains("Turn/bend cues:"), "evidence-linked discussion missing");
            check(!text.contains("You failed") && !text.contains("score") && !text.contains("braked"), "invented learner judgement");
        });
        test("long sessions retain bounded phrases while exact accepted totals remain", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            for (int i = 1; i <= 5000; i++) session.accepted(i, i % 2 == 0 ? 4 : 5, "Cue " + i);
            session.accepted(5001, 5, "Cue 1");
            PracticeSessionReview.Review review = session.finish(5002);
            check(review.cues.size() == 12, "phrase storage grows without bound");
            check(review.roadCues == 2500 && review.navigationCues == 2501, "bounded storage discarded the accepted totals");
            check(review.cues.get(0).count == 2, "repeated retained phrase lost its count");
            check(review.text().contains("Other cues:"), "undisplayed accepted cues silently omitted");
            try {review.cues.clear();throw new AssertionError("review snapshot is mutable");}
            catch (UnsupportedOperationException expected) {}
        });
        test("source switches or backgrounding cannot complete or contaminate a road lesson", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            session.accepted(1, 4, "Give space");session.discard();
            check(session.finish(2) == null && !session.accepted(2, 4, "MP4 cue"), "test/background input entered a completed road review");
            session.start(3);
            check(session.finish(4).roadCues == 0, "discarded road session leaked to another input");
            check(session.finish(5) == null, "same session completed twice");
        });
        test("empty or unobserved sessions preserve uncertainty and do not invent takeaways", () -> {
            PracticeSessionReview session = new PracticeSessionReview();session.start(0);
            String text = session.finish(30).text();
            check(text.contains("Estimated camera availability: 0%") && text.contains("Unmeasured time: 30 sec"), "no measurements look fully available");
            check(text.contains("No cues does not mean there were no hazards"), "silent session becomes an all-clear");
            check(!text.contains("Space cues:") && !text.contains("Turn/bend cues:") && !text.contains("Vehicle cues:"), "unobserved faults or lessons invented");
            check(text.contains("freshness, not accuracy") && text.contains("do not establish driving skill or safety"), "review overstates what was measured");
        });
        test("review access blocks movement and monitoring and requires parking confirmation for unknown speed", () -> {
            for (boolean monitoring : new boolean[]{false, true})
                for (boolean moving : new boolean[]{false, true})
                    for (boolean hasReview : new boolean[]{false, true})
                        for (boolean stopped : new boolean[]{false, true}) {
                            PracticeSessionReview.Access actual = PracticeSessionReview.access(monitoring, moving, hasReview, stopped);
                            if (monitoring || moving || !hasReview)
                                check(actual == PracticeSessionReview.Access.UNAVAILABLE, "review opened during movement/monitoring or without a completed session");
                            else check(actual == (stopped ? PracticeSessionReview.Access.READY : PracticeSessionReview.Access.CONFIRM_PARKED),
                                "unknown speed silently treated as parked or measured stop ignored");
                        }
        });
        System.out.println(passed + " practice session review tests passed");
    }
}
