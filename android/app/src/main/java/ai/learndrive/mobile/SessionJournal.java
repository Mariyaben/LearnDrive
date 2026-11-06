package ai.learndrive.mobile;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;

/** Opt-in, bounded evidence writer. Producers never perform file IO or wait for space. */
public final class SessionJournal {
    interface Opener {OutputStream open(File file) throws IOException;}
    public static final long DEFAULT_LIMIT = 8L*1024*1024;
    private final ArrayBlockingQueue<String> queue;
    private final AtomicLong dropped = new AtomicLong(), written = new AtomicLong();
    private final Thread worker;
    private volatile boolean accepting = true, finished, limitReached;
    private volatile String error = "", reason = "user_stop";
    public final String sessionId = UUID.randomUUID().toString();

    public SessionJournal(File file, String metadata) {
        this(file,metadata,128,DEFAULT_LIMIT,f->Files.newOutputStream(f.toPath(),StandardOpenOption.CREATE_NEW,StandardOpenOption.WRITE));
    }
    SessionJournal(File file,String metadata,int capacity,long limit,Opener opener) {
        if(capacity<1||limit<2048||!singleObject(metadata))throw new IllegalArgumentException("Invalid journal configuration");
        queue=new ArrayBlockingQueue<>(capacity);
        worker=new Thread(()->write(file,metadata,limit,opener),"session-evidence");worker.start();
    }
    private static boolean singleObject(String value) {
        return value!=null&&value.length()<=16384&&value.startsWith("{")&&value.endsWith("}")&&
            value.indexOf('\n')<0&&value.indexOf('\r')<0;
    }
    public synchronized boolean offer(String record) {
        if(!accepting)return false;
        if(!singleObject(record)||!queue.offer(record)){dropped.incrementAndGet();return false;}
        return true;
    }
    public synchronized void stop(String stopReason) {
        if(!accepting)return;
        reason="backgrounded".equals(stopReason)?"backgrounded":"activity_destroyed".equals(stopReason)?"activity_destroyed":"user_stop";
        accepting=false;
    }
    public synchronized void recordLoss(){if(accepting)dropped.incrementAndGet();}
    public boolean isAccepting(){return accepting;}
    public boolean isFinished(){return finished;}
    public long dropped(){return dropped.get();}
    public String error(){return error;}
    public String status(){
        if(!error.isEmpty())return "Write failed; any saved evidence is partial";
        if(limitReached)return "8 MiB limit reached; partial capture saved";
        String state=finished?"Stopped":accepting?"Recording locally":"Finishing";
        return state+" · "+written.get()+" records · "+dropped.get()+" dropped";
    }
    // For tests/background callers only. Never called on the Android UI/rules thread.
    public boolean awaitStopped(long milliseconds) throws InterruptedException {worker.join(milliseconds);return finished;}
    private void write(File file,String metadata,long limit,Opener opener) {
        try {
            File parent=file.getParentFile();
            if(parent!=null&&!parent.isDirectory()&&!parent.mkdirs())throw new IOException("Cannot create evidence directory");
            byte[] header=("{\"schema_version\":1,\"event\":\"session_start\",\"session_id\":\""+sessionId+"\",\"metadata\":"+metadata+"}\n").getBytes(StandardCharsets.UTF_8);
            if(header.length+1024>limit)throw new IOException("Metadata exceeds evidence budget");
            try(OutputStream out=new BufferedOutputStream(opener.open(file))) {
                long bytes=header.length;out.write(header);out.flush();
                while(accepting||!queue.isEmpty()) {
                    String record=queue.poll(50,TimeUnit.MILLISECONDS);
                    if(record==null)continue;
                    byte[] line=(record+"\n").getBytes(StandardCharsets.UTF_8);
                    if(bytes+line.length+1024>limit) {
                        synchronized(this){limitReached=true;accepting=false;reason="size_limit";dropped.addAndGet(1+queue.size());queue.clear();}
                        break;
                    }
                    out.write(line);bytes+=line.length;written.incrementAndGet();
                    // Bound crash loss without making producers wait on storage.
                    if(written.get()%16==0)out.flush();
                }
                String footer="{\"schema_version\":1,\"event\":\"session_end\",\"session_id\":\""+sessionId+
                    "\",\"complete\":"+(dropped.get()==0&&!limitReached)+",\"stop_reason\":\""+reason+
                    "\",\"records_written\":"+written.get()+",\"records_dropped\":"+dropped.get()+",\"limit_reached\":"+limitReached+"}\n";
                out.write(footer.getBytes(StandardCharsets.UTF_8));
            }
        }catch(Exception ex){error=ex.getClass().getSimpleName()+": "+ex.getMessage();}
        finally {synchronized(this){accepting=false;queue.clear();finished=true;}}
    }
}
