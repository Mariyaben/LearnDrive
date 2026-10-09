import Foundation
import AVFoundation

final class AudioWorker:NSObject {
    private let core:LDCore
    private let synth=AVSpeechSynthesizer()
    private let queue=DispatchQueue(label:"pcm-audio")
    private let lock=NSLock()
    private let engine=AVAudioEngine()
    private let player=AVAudioPlayerNode()
    private var clips:[Int:AVAudioPCMBuffer]=[:]
    private var active=0
    private var generation=0
    private var cancelled=false
    var onStatus:((String)->Void)?
    var ready:Bool {lock.lock();defer{lock.unlock()};return clips.count==Int(core.promptCount()) && !cancelled}
    init(core:LDCore) {
        self.core=core
        super.init()
        engine.attach(player)
        DispatchQueue.main.async {[weak self] in self?.warm(1)}
        NotificationCenter.default.addObserver(self,selector:#selector(interrupted),name:AVAudioSession.interruptionNotification,object:nil)
    }
    @objc private func interrupted(){cancel()}
    private func warm(_ id:Int) {
        guard id<=Int(core.promptCount()) else{onStatus?("Offline PCM ready");return}
        lock.lock();let stopped=cancelled;lock.unlock();guard !stopped else{return}
        guard let voice=AVSpeechSynthesisVoice(language:"en-US") else{onStatus?("Install an English voice in system settings");return}
        let utterance=AVSpeechUtterance(string:core.promptText(Int32(id)));utterance.voice=voice
        var chunks:[AVAudioPCMBuffer]=[]
        synth.write(utterance) {[weak self] buffer in
            guard let self=self,let pcm=buffer as? AVAudioPCMBuffer else{return}
            if pcm.frameLength==0 {
                guard let first=chunks.first else{self.onStatus?("Offline synthesis produced no audio");return}
                let total=chunks.reduce(0) { $0+Int($1.frameLength) }
                guard first.format.commonFormat == .pcmFormatFloat32,!first.format.isInterleaved,
                      let merged=AVAudioPCMBuffer(pcmFormat:first.format,frameCapacity:AVAudioFrameCount(total)),
                      let destination=merged.floatChannelData else{self.onStatus?("Unsupported PCM voice format");return}
                merged.frameLength=AVAudioFrameCount(total);var offset=0
                for chunk in chunks {
                    guard chunk.format==first.format,let source=chunk.floatChannelData else{self.onStatus?("Inconsistent PCM voice format");return}
                    for c in 0..<Int(first.format.channelCount){destination[c].advanced(by:offset).update(from:source[c],count:Int(chunk.frameLength))}
                    offset+=Int(chunk.frameLength)
                }
                self.lock.lock();self.clips[id]=merged;self.lock.unlock()
                DispatchQueue.main.async {self.warm(id+1)}
            }else {
                // The callback's buffer may be reused; own a copy during warmup.
                guard let owned=AVAudioPCMBuffer(pcmFormat:pcm.format,frameCapacity:pcm.frameLength),
                      let src=pcm.floatChannelData,let dst=owned.floatChannelData else{self.onStatus?("Unsupported PCM buffer");return}
                owned.frameLength=pcm.frameLength
                for c in 0..<Int(pcm.format.channelCount){dst[c].update(from:src[c],count:Int(pcm.frameLength))}
                chunks.append(owned)
            }
        }
    }
    func submit(_ id:Int)->Bool {
        lock.lock()
        guard !cancelled,core.promptMayInterrupt(Int32(id),current:Int32(active)),let clip=clips[id] else{lock.unlock();return false}
        generation+=1;let token=generation;active=id;lock.unlock()
        // Serial output worker owns engine/node calls. A high-priority prompt
        // stops and replaces the previous buffer; inference never waits for PCM.
        queue.async {[weak self] in
            guard let self=self else{return}
            self.lock.lock();let valid=token==self.generation && !self.cancelled;self.lock.unlock();guard valid else{return}
            do {
                let session=AVAudioSession.sharedInstance()
                try session.setCategory(.playback,mode:.voicePrompt,options:[.duckOthers])
                try session.setActive(true)
                self.player.stop()
                if !self.engine.isRunning {
                    self.engine.connect(self.player,to:self.engine.mainMixerNode,format:clip.format)
                    self.engine.prepare();try self.engine.start()
                }
                self.player.scheduleBuffer(clip,completionCallbackType:.dataPlayedBack) {[weak self] _ in
                    guard let self=self else{return}
                    self.lock.lock();if self.generation==token{self.active=0};self.lock.unlock()
                }
                self.player.play()
            }catch{
                self.lock.lock();if self.generation==token{self.active=0};self.lock.unlock()
                self.onStatus?("Audio output: \(error.localizedDescription)")
            }
        }
        return true
    }
    func cancel() {
        lock.lock();generation+=1;active=0;lock.unlock()
        queue.async {[weak self] in self?.player.stop()}
    }
    func close() {
        lock.lock();cancelled=true;lock.unlock();cancel()
        DispatchQueue.main.async {[weak self] in self?.synth.stopSpeaking(at:.immediate)}
        queue.async {[weak self] in self?.engine.stop();try? AVAudioSession.sharedInstance().setActive(false,options:.notifyOthersOnDeactivation)}
        NotificationCenter.default.removeObserver(self)
    }
}
