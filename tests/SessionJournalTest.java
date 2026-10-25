package ai.learndrive.mobile;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.concurrent.CountDownLatch;

public final class SessionJournalTest {
    private static void check(boolean ok,String why){if(!ok)throw new AssertionError(why);}
    public static void main(String[] args) throws Exception {
        File root=Files.createTempDirectory(new File(args[0]).toPath(),"journal-test-").toFile();
        File file=new File(root,"session.jsonl");SessionJournal journal=new SessionJournal(file,"{\"test\":true}");
        check(journal.offer("{\"schema_version\":1,\"event\":\"decision\",\"sequence\":1}"),"first row rejected");
        journal.stop("backgrounded");check(!journal.offer("{}"),"recorded after stop");
        check(journal.awaitStopped(5000),"writer failed to drain");
        String saved=Files.readString(file.toPath());
        check(saved.contains("\"complete\":true")&&saved.contains("\"stop_reason\":\"backgrounded\"")&&saved.contains("\"records_written\":1"),"clean capture/footer wrong");
        SessionJournal duplicate=new SessionJournal(file,"{}");duplicate.stop("user_stop");duplicate.awaitStopped(5000);
        check(!duplicate.error().isEmpty()&&Files.readString(file.toPath()).equals(saved),"overwrote prior evidence");
        CountDownLatch gate=new CountDownLatch(1);ByteArrayOutputStream memory=new ByteArrayOutputStream();
        SessionJournal full=new SessionJournal(new File(root,"memory.jsonl"),"{}",1,4096,f->{
            try{if(!gate.await(5,java.util.concurrent.TimeUnit.SECONDS))throw new IOException("test timeout");}catch(InterruptedException ex){throw new IOException(ex);}return memory;});
        check(full.offer("{}")&&!full.offer("{}")&&full.dropped()==1,"queue did not report loss");
        full.stop("user_stop");gate.countDown();check(full.awaitStopped(5000),"full queue cannot stop");
        check(memory.toString(StandardCharsets.UTF_8).contains("\"complete\":false"),"loss reported as complete");
        ByteArrayOutputStream limited=new ByteArrayOutputStream();SessionJournal capped=new SessionJournal(new File(root,"cap.jsonl"),"{}",8,2048,f->limited);
        capped.offer("{\"data\":\""+"x".repeat(1600)+"\"}");capped.stop("user_stop");capped.awaitStopped(5000);
        check(limited.size()<=2048&&limited.toString(StandardCharsets.UTF_8).contains("\"limit_reached\":true"),"file cap not enforced");
        SessionJournal failed=new SessionJournal(new File(root,"fail.jsonl"),"{}",8,4096,f->{throw new IOException("disk unavailable");});
        failed.awaitStopped(5000);check(!failed.isAccepting()&&!failed.error().isEmpty(),"IO failure stayed recording");
        SessionJournal invalid=new SessionJournal(new File(root,"invalid.jsonl"),"{}");
        check(!invalid.offer("{}\n{}")&&!invalid.offer("x".repeat(17000)),"unbounded/multiline record accepted");invalid.recordLoss();invalid.stop("user_stop");invalid.awaitStopped(5000);
        check(Files.readString(new File(root,"invalid.jsonl").toPath()).contains("\"records_dropped\":3"),"rejected/serialization-loss records hidden");
        System.out.println("Journal checks passed: drain, stop, existing evidence, bounded queue/file, loss, IO failure and invalid records");
    }
}
