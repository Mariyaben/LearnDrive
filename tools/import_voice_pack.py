"""Put a recorded coach voice into the app: each take compressed to Opus (32 kbit/s, about 2 MB per
language) under android/app/src/voice/assets/voice/<language>/, with manifest.json (the words of
each take). That folder is git-ignored (the repository is public); the app unpacks the takes once
on the phone and speaks with them; a phrase with no take is said by the artificial voice.

Usage: python -I tools/import_voice_pack.py <pack folder or .zip> [name of this version]
A pack is what Record my voice exported: manifest.json and one WAV per take. Needs ffmpeg with libopus.
"""
import json, os, shutil, subprocess, sys, tempfile, zipfile

def main():
    source = sys.argv[1]
    name = sys.argv[2] if len(sys.argv) > 2 else os.path.basename(os.path.normpath(source))
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    work = None
    if source.lower().endswith('.zip'):
        work = tempfile.mkdtemp(prefix='voice-pack-')
        with zipfile.ZipFile(source) as z:
            for member in z.namelist():   # only the pack's own files, never paths outside it
                if member == 'manifest.json' or (member.count('/') == 1 and member.endswith('.wav') and '..' not in member):
                    z.extract(member, work)
        source = work
    try:
        manifest = json.load(open(os.path.join(source, 'manifest.json'), encoding='utf-8'))
        for language, pack in manifest['languages'].items():
            takes = pack['takes']
            if not takes:
                continue
            out = os.path.join(root, 'android', 'app', 'src', 'voice', 'assets', 'voice', language)
            shutil.rmtree(out, ignore_errors=True); os.makedirs(out)
            entries = []
            for take in takes:
                opus = os.path.splitext(os.path.basename(take['file']))[0] + '.opus'
                subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(source, take['file']), '-ac', '1',
                                '-c:a', 'libopus', '-b:a', '32k', '-application', 'voip', os.path.join(out, opus)], check=True)
                entries.append({'file': opus, 'words': take['words'], 'english': take['english']})
            json.dump({'format': 'learndrive-app-voice', 'version': 1, 'language': language, 'name': name,
                       'recorded': manifest.get('created', ''), 'takes': entries},
                      open(os.path.join(out, 'manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
            size = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
            print('%s: %d takes, %.2f MB -> %s' % (language, len(entries), size / 1e6, out))
    finally:
        if work:
            shutil.rmtree(work, ignore_errors=True)

main()
