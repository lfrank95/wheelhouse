import numpy as np, sys, time
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from bem3d import QuadMesh, solve, far_field, sphere, RHO, C
from scipy.special import spherical_jn, spherical_yn
h1=lambda n,x: spherical_jn(n,x)+1j*spherical_yn(n,x); h1d=lambda n,x: spherical_jn(n,x,True)+1j*spherical_yn(n,x,True)
a=0.2
for f,nl in ((300,16),(858,16),(1000,24)):
    V,Q=sphere(a,nl,2*nl); m=QuadMesh(V,Q); k=2*np.pi*f/C; w=2*np.pi*f
    for mode in (0,1):
        ct=m.c[:,2]/np.linalg.norm(m.c,axis=1)
        m.vn=(np.ones(len(Q)) if mode==0 else ct).astype(complex)
        t0=time.time(); p,q,kk=solve(m,f,chief=[(0.03,0.05,0.02),(-0.06,0.02,-0.05),(0.08,-0.04,0.1)]); dt=time.time()-t0
        pex= (1j*w*RHO*a/(1j*k*a-1))*np.ones_like(ct) if mode==0 else 1j*w*RHO/(k*h1d(1,k*a))*h1(1,k*a)*ct
        print(f"f {f} N {len(Q)} mode {mode}: surface err {np.linalg.norm(p-pex)/np.linalg.norm(pex):.2e}  {dt:.1f}s")
