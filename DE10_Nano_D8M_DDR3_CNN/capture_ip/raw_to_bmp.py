"""Convert the default 480x270 little-endian 0x00RRGGBB snapshot to BMP.
No third-party Python packages are required.
"""
import argparse
import struct
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('input');p.add_argument('output')
p.add_argument('--width',type=int,default=480)
p.add_argument('--height',type=int,default=270)
a=p.parse_args()
data=Path(a.input).read_bytes()
if a.width<=0 or a.height<=0 or len(data)!=a.width*a.height*4:
    raise SystemExit('Input length does not match width x height x 4 bytes.')
rowbytes=(a.width*3+3)&~3
pixels=bytearray()
for y in range(a.height-1,-1,-1):
    row=data[y*a.width*4:(y+1)*a.width*4]
    for x in range(a.width):pixels.extend(row[x*4:x*4+3])
    pixels.extend(bytes(rowbytes-a.width*3))
header=struct.pack('<2sIHHI',b'BM',54+len(pixels),0,0,54)
dib=struct.pack('<IiiHHIIiiII',40,a.width,a.height,1,24,0,len(pixels),2835,2835,0,0)
Path(a.output).write_bytes(header+dib+pixels)
print(f'Saved {a.width}x{a.height} image: {a.output}')
