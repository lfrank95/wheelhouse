import numpy as np, sys
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
import horn
from axibem import solve, far_field, C
TH=np.radians([0,45,90,135,180])
for f in (2000, 6300, 12500):
    for hf,T in ((12,3e-3),(24,3e-3),(12,5e-3)):
        horn.T=T; m,ch=horn.build('g10', min(4e-3, C/f/hf)); p,q,k=solve(m,f,chief=ch); F=np.abs(far_field(m,p,q,k,TH))
        print(f, 'h=lam/%d T=%.0fmm N=%d'%(hf,T*1e3,len(m.len)), ' '.join('%.1f'%v for v in 20*np.log10(F/F[0])), ' on-axis %.2f dB'%(20*np.log10(F[0])))
