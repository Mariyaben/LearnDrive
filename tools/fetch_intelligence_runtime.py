"""Provision pinned publisher artifacts locally. Weights/binaries stay outside Git."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
ARTIFACTS={
 "qwen3-0.6b-q8": {
  "url":"https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/23749fefcc72300e3a2ad315e1317431b06b590a/Qwen3-0.6B-Q8_0.gguf",
  "filename":"Qwen3-0.6B-Q8_0.gguf","bytes":639446688,
  "sha256":"9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031","license":"Apache-2.0"},
 "llama-windows": {
  "url":"https://github.com/ggml-org/llama.cpp/releases/download/b11932/llama-b11932-bin-win-cpu-x64.zip",
  "filename":"llama-b11932-bin-win-cpu-x64.zip","bytes":24255745,
  "sha256":"1b57df69f58dc1bfab83c4743a443b1c4973e7f83283ac7a39239e496815d0a3","license":"MIT"},
 "llama-android": {
  "url":"https://github.com/ggml-org/llama.cpp/releases/download/b11932/llama-b11932-bin-android-arm64.tar.gz",
  "filename":"llama-b11932-bin-android-arm64.tar.gz","bytes":77421525,
  "sha256":"bd3763d6b8fb85de145302fa50d66b4d1877346f5d4c9727567c6c4e925cd57d","license":"MIT"},
 "smollm2-360m-q8": {
  "url":"https://huggingface.co/HuggingFaceTB/SmolLM2-360M-Instruct-GGUF/resolve/593b5a2e04c8f3e4ee880263f93e0bd2901ad47f/smollm2-360m-instruct-q8_0.gguf",
  "filename":"smollm2-360m-instruct-q8_0.gguf","bytes":386404992,
  "sha256":"48ab3034d0dd401fbc721eb1df3217902fee7dab9078992d66431f09b7750201","license":"Apache-2.0"}}
HEADERS={'llama.h':'c0854771d3760a41642c4e59429c5b3b9fc774887c59d37eeadc4527681732cd',
 'gguf.h':'e56714aab702e5ce62ee587a409643c08f7e93e8fbb77f48ef7cc85075f96fa4',
 'ggml.h':'2b4dd92fa992cb4ef484917a6c62fef5097698744160ccc09214fd0c459c92c7',
 'ggml-opt.h':'3586de1bc8a934b5c72339e2b6937b0641e8f149b512231e666f67de0736eea2',
 'ggml-backend.h':'0e5e8e1f16d11ebbf501e38bc6705d79eee819b5f0b17476533f2100ed9674e2',
 'ggml-alloc.h':'d114067f9099c4a0e2bbc0625c676d4894d9cf0ea27168ee072c7de0e71bb51d',
 'ggml-cpu.h':'316279e004cdeb8e6ef78599acb602bf79a8abdf897fed9fd1914808c1518c6e'}


def headers(folder):
 out=folder/'include';out.mkdir(parents=True,exist_ok=True)
 for name,expected in HEADERS.items():
  path=out/name
  if path.is_file() and digest(path)==expected:continue
  prefix='include/' if name=='llama.h' else 'ggml/include/'
  url='https://raw.githubusercontent.com/ggml-org/llama.cpp/0c8c59ef3d1b7a5c073f3cd1c2fe28d4fa960c89/'+prefix+name
  data=urllib.request.urlopen(url,timeout=30).read()
  if hashlib.sha256(data).hexdigest()!=expected:raise ValueError('Header provenance mismatch: '+name)
  path.write_bytes(data)
 print('verified pinned llama headers',flush=True)


def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as source:
  for block in iter(lambda:source.read(1048576),b''):h.update(block)
 return h.hexdigest()


def fetch(name,folder):
 a=ARTIFACTS[name]; path=folder/a['filename'];folder.mkdir(parents=True,exist_ok=True)
 if not path.is_file() or path.stat().st_size!=a['bytes'] or digest(path)!=a['sha256']:
  temp=path.with_suffix(path.suffix+'.partial');h=hashlib.sha256();size=0;next_progress=67108864
  request=urllib.request.Request(a['url'],headers={'User-Agent':'LearnDrive-local-intelligence/2'})
  with urllib.request.urlopen(request,timeout=90) as source,temp.open('wb') as out:
   while block:=source.read(1048576):
    out.write(block);h.update(block);size+=len(block)
    if size>=next_progress:print(name,size,'bytes',flush=True);next_progress+=67108864
  if size!=a['bytes'] or h.hexdigest()!=a['sha256']:raise ValueError('Publisher artifact integrity mismatch: '+name)
  temp.replace(path)
 if path.suffix=='.zip':
  target=folder/'windows';target.mkdir(exist_ok=True)
  with zipfile.ZipFile(path) as archive:
   for item in archive.infolist():
    resolved=(target/item.filename).resolve()
    if not resolved.is_relative_to(target.resolve()):raise ValueError('Archive path escapes output')
   archive.extractall(target)
 elif path.name.endswith('.tar.gz'):
  target=folder/'android';target.mkdir(exist_ok=True)
  with tarfile.open(path) as archive:
   for member in archive.getmembers():
    if not (target/member.name).resolve().is_relative_to(target.resolve()) or member.issym() or member.islnk():
     raise ValueError('Unsafe archive member: '+member.name)
   archive.extractall(target)
 (folder/(name+'.provenance.json')).write_text(json.dumps(a,indent=2)+'\n')
 print('verified',name,path,flush=True)
 return path


if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('artifacts',nargs='+',choices=[*ARTIFACTS,'llama-headers'])
 parser.add_argument('--output',type=Path,default=ROOT/'.tools/intelligence')
 args=parser.parse_args()
 for name in args.artifacts:
  if name=='llama-headers':headers(args.output)
  else:fetch(name,args.output)
