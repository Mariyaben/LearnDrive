"""Clean a LearnDrive voice pack (from Record my voice): remove the steady background noise of the
room the takes were recorded in, keep the voice. Standard speech enhancement (Ephraim-Malah
log-spectral amplitude estimator, decision-directed a priori SNR), each take's own background
bounded by the room's (learnt from all takes), a high-pass below the voice, and the takes levelled
to the coach's loudness again. It cannot remove room echo or sounds on top of the words.

Usage: python -I tools/voice_clean.py <pack folder> <out folder> [max attenuation dB] [noise over-estimate]
  standard: 20 1      strong (the 2026-11-18 voice in 0.9.1): 25 2.5
"""
import json, os, shutil, sys, wave
import numpy as np
from scipy import signal, special

RATE = 22050
N_FFT, HOP = 512, 128

def read(path):
    w = wave.open(path, 'rb'); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.float64) / 32768.0
    w.close(); return x

def write(path, x):
    pcm = np.clip(np.round(x * 32768.0), -32768, 32767).astype('<i2')
    w = wave.open(path, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE); w.writeframes(pcm.tobytes()); w.close()

def stft(x):
    _, _, z = signal.stft(x, fs=RATE, window='hann', nperseg=N_FFT, noverlap=N_FFT - HOP, boundary='even', padded=True)
    return z

def istft(z, length):
    _, x = signal.istft(z, fs=RATE, window='hann', nperseg=N_FFT, noverlap=N_FFT - HOP, boundary=True)
    return x[:length]

def quiet_frames(power):
    """Frames of a take with no voice: the quietest 15 %, away from the faded ends."""
    energy = power.sum(axis=0)
    inner = np.arange(power.shape[1])[3:-3] if power.shape[1] > 12 else np.arange(power.shape[1])
    cut = np.quantile(energy[inner], .15)
    return [i for i in inner if energy[i] <= cut]

def lsa(z, noise, floor_db, alpha=.98):
    """Log-spectral amplitude gains (Ephraim & Malah 1985), decision-directed a priori SNR."""
    power = np.abs(z) ** 2
    gmin = 10 ** (-floor_db / 20); ximin = 10 ** (-25 / 10)
    gains = np.empty_like(power); prev = np.ones(power.shape[0]); prev_gamma = np.ones(power.shape[0])
    for t in range(power.shape[1]):
        gamma = np.minimum(power[:, t] / noise, 1e4)
        xi = np.maximum(alpha * prev ** 2 * prev_gamma + (1 - alpha) * np.maximum(gamma - 1, 0), ximin)
        v = np.maximum(xi / (1 + xi) * gamma, 1e-8)
        g = xi / (1 + xi) * np.exp(.5 * special.exp1(v))
        g = np.clip(g, gmin, 1.0)
        gains[:, t] = g; prev = g; prev_gamma = gamma
    # A little smoothing across neighbouring frequencies keeps the leftover noise from warbling.
    gains = signal.convolve2d(gains, np.array([[.25], [.5], [.25]]), mode='same', boundary='symm')
    return z * np.maximum(gains, gmin)

def level(x):
    """Voiced RMS at the coach's loudness (about -17.5 dBFS), peaks below -1 dBFS, 10 ms fades."""
    win = RATE // 50; k = len(x) // win
    rms = np.sqrt((x[:k * win].reshape(k, win) ** 2).mean(axis=1)) if k else np.array([0.0])
    threshold = max(np.quantile(rms, .2) * 3, 400 / 32768)
    voiced = np.abs(x) > threshold
    gain = .1334 / np.sqrt((x[voiced] ** 2).mean()) if voiced.any() else 1.0
    peak = np.abs(x).max() + 1e-9
    gain = min(gain, (29200 / 32768) / peak)
    y = x * gain; fade = RATE // 100
    ramp = np.linspace(0, 1, fade)
    y[:fade] *= ramp; y[-fade:] *= ramp[::-1]
    return y

def main():
    src, dst = sys.argv[1], sys.argv[2]
    floor_db = float(sys.argv[3]) if len(sys.argv) > 3 else 20.0
    over = float(sys.argv[4]) if len(sys.argv) > 4 else 1.0   # >1 removes more, at some risk to soft sounds
    manifest = json.load(open(os.path.join(src, 'manifest.json'), encoding='utf-8'))
    hp = signal.butter(2, 80, 'highpass', fs=RATE, output='sos')
    takes = [(lang, t) for lang, v in manifest['languages'].items() for t in v['takes']]
    audio = {}
    pooled = []
    for lang, t in takes:
        x = signal.sosfiltfilt(hp, read(os.path.join(src, t['file'])))
        z = stft(x); p = np.abs(z) ** 2
        frames = quiet_frames(p)
        audio[t['file']] = (x, z, p[:, frames])
        pooled.append(p[:, frames])
    # The room's noise: the median of every quiet frame of every take (a median of noise power is
    # about 0.69 of its mean, hence 1.44).
    room = 1.44 * np.median(np.concatenate(pooled, axis=1), axis=1) + 1e-12
    os.makedirs(dst, exist_ok=True)
    for lang, t in takes:
        x, z, quiet = audio[t['file']]
        # Each take's own background: per frequency, the quieter fifth of its (lightly smoothed)
        # frames, corrected to a mean. Takes differ (a fan, passing traffic), so the room's noise
        # only bounds it: never below the room, never above four times it (that would be voice).
        power = signal.convolve2d(np.abs(z) ** 2, np.ones((1, 5)) / 5, mode='same', boundary='symm')
        own = 1.5 * np.quantile(power, .2, axis=1)
        noise = np.clip(signal.savgol_filter(own, 9, 2), room, 4 * room)
        y = level(istft(lsa(z, noise * over, floor_db), len(x)))
        os.makedirs(os.path.join(dst, os.path.dirname(t['file'])), exist_ok=True)
        write(os.path.join(dst, t['file']), y)
    shutil.copy(os.path.join(src, 'manifest.json'), os.path.join(dst, 'manifest.json'))
    print('cleaned', len(takes), 'takes; max attenuation', floor_db, 'dB; noise x', over)

main()
