#!/usr/bin/env python3
"""The reference values source/engine/render/tests/BrdfTests.cpp checks the shaders' BRDF against.

The glTF 2.0 specification's appendix B formulas, evaluated independently of
assets/shaders/include/brdf.glsl in double precision, and the split sum's scale and bias by plain
midpoint quadrature over the light's hemisphere (800 by 800 in theta and phi) rather than by the
importance sampling the shader uses. It takes about a minute.
"""
import math
def norm(v):
    l=math.sqrt(sum(x*x for x in v)); return [x/l for x in v]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def D(nh,a):
    a2=a*a; d=nh*nh*(a2-1)+1; return a2/(math.pi*d*d)
def V(nl,nv,a):
    a2=a*a
    return 0.5/(nl*math.sqrt(nv*nv*(1-a2)+a2)+nv*math.sqrt(nl*nl*(1-a2)+a2))
print("D(0.9,0.5)", D(0.9,0.5))
print("D(1,0.25)", D(1,0.25))
print("V(0.5,0.8,0.3)", V(0.5,0.8,0.3))
# BRDF*cos at a configuration
base=[0.8,0.6,0.4]; metal=0.25; rough=0.5; a=rough*rough
n=[0,0,1]; v=norm([0.3,0,1]); l=norm([-0.5,0.2,1])
h=norm([x+y for x,y in zip(l,v)])
nl=dot(n,l); nv=dot(n,v); nh=dot(n,h); vh=dot(v,h)
f0=[0.04*(1-metal)+b*metal for b in base]
F=[f+(1-f)*(1-vh)**5 for f in f0]
cdiff=[b*(1-metal) for b in base]
res=[((1-Fi)*c/math.pi + Fi*D(nh,a)*V(nl,nv,a))*nl for Fi,c in zip(F,cdiff)]
print("brdf", res)
# LUT by quadrature over l
def lut(nv, rough, N=800):
    a=rough*rough
    v=[math.sqrt(1-nv*nv),0,nv]
    s=b=0.0
    dt=(math.pi/2)/N; dp=math.pi/N
    for i in range(N):
        t=(i+0.5)*dt; st=math.sin(t); ct=math.cos(t)
        for j in range(N):
            p=(j+0.5)*dp
            l=[st*math.cos(p),st*math.sin(p),ct]
            h=norm([l[0]+v[0],l[1]+v[1],l[2]+v[2]])
            vh=dot(v,h); nh=h[2]
            fc=(1-vh)**5
            val=D(nh,a)*V(ct,nv,a)*ct*st*dt*dp*2  # symmetric in phi
            s+=(1-fc)*val; b+=fc*val
    return s,b
for p in [(0.5,0.5),(0.25,0.75),(0.9,0.3),(0.7,1.0)]:
    print("lut",p,lut(*p))
