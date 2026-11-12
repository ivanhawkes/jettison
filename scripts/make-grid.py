#!/usr/bin/env python3
import numpy as np
from PIL import Image

N=2048
y,x=np.mgrid[0:N,0:N]
light=np.array([132,132,132]); dark=np.array([108,114,110])
chk=((x//64)+(y//64))%2==0
img=np.where(chk[...,None],light,dark).astype(np.int16)

# faint fine subdivision every 8px
fine=(x%8==0)|(y%8==0)
img[fine & chk]-=14
img[fine & ~chk]+=12
img=np.clip(img,0,255).astype(np.uint8)
def line_mask(c, step, w):
    # pixels within a w-wide band centred on multiples of step (wraps -> seamless)
    off=(c + w//2) % step
    return off < w
for step,w in [(128,1),(256,2),(512,3),(1024,4)]:
    m=line_mask(x,step,w)|line_mask(y,step,w)
    img[m]=0

# diamond markers every 128px along the 1024 lines (centre cross + tile edges)
r=6
for a in range(0,N,1024):
    for b in range(0,N,128):
        for (cx,cy) in [(a,b),(b,a)]:
            dx=(x-cx+N//2)%N-N//2; dy=(y-cy+N//2)%N-N//2
            img[(abs(dx)+abs(dy))<=r]=0
Image.fromarray(img).save('../assets/textures/debug/floor-grid/floor-grid-color.png',optimize=True)
