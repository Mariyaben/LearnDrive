"""Verify asset provenance and compare the native temporal run with Python ORT."""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
import onnxruntime as ort

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--native-prefix',type=Path)
args=parser.parse_args()
assets=ROOT/'android/app/src/main/assets'
manifest=json.loads((assets/'supercombo-manifest.json').read_text())
model=assets/'supercombo.onnx'
assert model.stat().st_size==manifest['bytes']
assert hashlib.sha256(model.read_bytes()).hexdigest()==manifest['sha256']
options=ort.SessionOptions();options.intra_op_num_threads=2;options.inter_op_num_threads=1
session=ort.InferenceSession(str(model),options,providers=['CPUExecutionProvider'])
state=np.zeros((1,512),np.float32)
previous=None
for frame in range(3):
    yy,xx=np.indices((256,512))
    y=(16+(yy+xx+frame*7)%220).astype(np.float32)
    chroma=np.full((128,256),128,np.float32)
    current=np.stack((y[0::2,0::2],y[1::2,0::2],y[0::2,1::2],y[1::2,1::2],chroma,chroma))
    packed=np.concatenate((current if previous is None else previous,current))[None]
    output=session.run(None,{'input_imgs':packed,'desire':np.zeros((1,8),np.float32),
        'traffic_convention':np.array([[0,1]],np.float32),'initial_state':state})[0]
    assert output.shape==(1,6472) and np.isfinite(output).all()
    state=output[:,5960:].copy();previous=current
if args.native_prefix:
    native_input=np.fromfile(str(args.native_prefix)+'-input.bin',np.float32).reshape(packed.shape)
    native_output=np.fromfile(str(args.native_prefix)+'-output.bin',np.float32).reshape(output.shape)
    np.testing.assert_array_equal(native_input,packed)
    np.testing.assert_allclose(native_output,output,rtol=1e-4,atol=1e-3)
    print('Native C++ warp/packing, 3-frame recurrent inference and Python ORT outputs agree')
else:
    print('Pinned Supercombo SHA-256, graph contract, finite 3-frame recurrent inference passed')
