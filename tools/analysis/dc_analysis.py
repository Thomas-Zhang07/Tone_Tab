import numpy as np
from scipy.signal import lfilter

SR = 48000.0

def db2g(db): return 10.0 ** (db / 20.0)

def biquad(kind, sr, f, q=0.7071, db=0.0):
    f = min(max(f, 10.0), sr * 0.49)
    A = 10.0 ** (db / 40.0)
    w0 = 2 * np.pi * f / sr
    cw, sw = np.cos(w0), np.sin(w0)
    alpha = sw / (2 * q)
    if kind == 'lp':
        b0, b1, b2 = (1-cw)/2, 1-cw, (1-cw)/2
        a0, a1, a2 = 1+alpha, -2*cw, 1-alpha
    elif kind == 'hp':
        b0, b1, b2 = (1+cw)/2, -(1+cw), (1+cw)/2
        a0, a1, a2 = 1+alpha, -2*cw, 1-alpha
    elif kind == 'peak':
        b0, b1, b2 = 1+alpha*A, -2*cw, 1-alpha*A
        a0, a1, a2 = 1+alpha/A, -2*cw, 1-alpha/A
    elif kind == 'loshelf':
        s = 2*np.sqrt(A)*alpha
        b0 = A*((A+1)-(A-1)*cw+s); b1 = 2*A*((A-1)-(A+1)*cw); b2 = A*((A+1)-(A-1)*cw-s)
        a0 = (A+1)+(A-1)*cw+s;     a1 = -2*((A-1)+(A+1)*cw);     a2 = (A+1)+(A-1)*cw-s
    elif kind == 'hishelf':
        s = 2*np.sqrt(A)*alpha
        b0 = A*((A+1)+(A-1)*cw+s); b1 = -2*A*((A-1)+(A+1)*cw); b2 = A*((A+1)+(A-1)*cw-s)
        a0 = (A+1)-(A-1)*cw+s;     a1 = 2*((A-1)-(A+1)*cw);      a2 = (A+1)-(A-1)*cw-s
    return np.array([b0/a0, b1/a0, b2/a0]), np.array([1.0, a1/a0, a2/a0])

def filt(x, coef):
    b, a = coef
    return lfilter(b, a, x)

def tone(sr=SR, secs=1.5, f=220.0, amp=0.3, start=0.5):
    n = int(sr*secs)
    x = np.zeros(n)
    s = int(sr*start)
    x[s:] = amp*np.sin(2*np.pi*f*np.arange(n-s)/sr)
    return x, s

def dc_db(y, s):
    # mean of last 0.75 s, relative to peak of the shaped waveform
    tail = y[s+int(0.5*SR):]
    m = np.mean(tail)
    return 20*np.log10(abs(m)/peak(tail)+1e-30) if peak(tail)>0 else -999, m

def peak(y): return np.max(np.abs(y)) if len(y) else 0

def report(name, y, s, peakref=None):
    tail = y[s+int(0.4*SR):]
    m = np.mean(tail)
    p = peak(tail) if peakref is None else peakref
    d = 20*np.log10(max(abs(m),1e-12)/max(p,1e-12))
    print(f"{name:44s} DC={m:+.5f}  peak={p:.3f}  DC/peak={d:6.1f} dB")
    return m

# ---------------- overdrive ----------------
def overdrive(model, drive=18.0, tonev=0.5, bass=0.0, level=-6.0, amp=0.3):
    x, s = tone(amp=amp)
    hp = {'ts808':720,'ts9':720,'klon':330,'sd1':720,'blues':220,'generic':600}[model]
    pre = filt(x, biquad('hp', SR, hp, 0.55 if model in ('ts808','ts9','sd1','generic') else 0.6))
    d = drive*0.9+4.0 if model=='klon' else (drive*0.6 if model=='blues' else drive)
    xd = pre*db2g(d)
    if model in ('ts808','ts9','generic','blues'):
        shaped = np.tanh(xd) * (0.92 if model=='ts9' else (0.85 if model=='blues' else 1.0))
    elif model=='klon':
        shaped = np.where(xd>=0, 1-np.exp(-xd), -(1-np.exp(xd*1.1))/1.05)
    elif model=='sd1':
        shaped = np.where(xd>=0, np.tanh(xd), np.tanh(1.7*xd)/1.7)
    clean = {'ts808':0.6,'ts9':0.6,'sd1':0.6,'blues':0.35}.get(model, 0.0)
    if model=='klon': clean = 1.2 + (0.15-1.2)*(drive/40.0)
    y = shaped + clean*x
    if model=='ts808':
        y = filt(y, biquad('lp',SR,800+4800*tonev**2,0.6)); y = filt(y, biquad('loshelf',SR,200,0.7071,bass)); y = filt(y, biquad('peak',SR,1000,0.8,1.5))
    elif model=='ts9':
        y = filt(y, biquad('lp',SR,1100+5500*tonev**2,0.7)); y = filt(y, biquad('loshelf',SR,200,0.7071,bass-1)); y = filt(y, biquad('peak',SR,2800,1.0,2.5))
    elif model=='klon':
        y = filt(y, biquad('lp',SR,7500,0.6)); y = filt(y, biquad('loshelf',SR,150,0.7071,1.0+bass)); y = filt(y, biquad('hishelf',SR,1300,0.7071,0.0))
    elif model=='sd1':
        y = filt(y, biquad('lp',SR,1000+5200*tonev**2,0.6)); y = filt(y, biquad('loshelf',SR,200,0.7071,bass)); y = filt(y, biquad('peak',SR,850,0.8,2.0))
    elif model=='blues':
        y = filt(y, biquad('lp',SR,1500+7000*tonev,0.5)); y = filt(y, biquad('loshelf',SR,150,0.7071,1.0+bass)); y = filt(y, biquad('peak',SR,1000,0.7,0.0))
    else:
        y = filt(y, biquad('lp',SR,1200+6000*tonev,0.7)); y = filt(y, biquad('loshelf',SR,200,0.7071,4.0+bass)); y = filt(y, biquad('peak',SR,1000,0.7,0.0))
    trim = {'ts808':0,'ts9':2.5,'klon':-4,'sd1':2,'blues':-0.5,'generic':0}[model]
    return y*db2g(level+trim), s

# ---------------- distortion ----------------
def hardish(x):
    t = np.clip(x, -1.5, 1.5)
    return t - 0.148148*t**3

def distortion(model, gain=34.0, tonev=0.55, mid=-3.0, tight=180.0, level=-14.0, amp=0.3):
    x, s = tone(amp=amp)
    pre = filt(x, biquad('hp', SR, tight, 0.7))
    g = gain + (4.0 if model=='rat' else 0.0)
    xd = pre*db2g(g)
    if model=='rat':
        shaped = hardish(xd)
    elif model=='ds1':
        shaped = np.where(xd>=0, hardish(xd), hardish(1.4*xd)*0.8)
    else:
        y0 = np.where(xd>=0, np.tanh(xd), np.clip(xd*0.9,-0.8,0.0)+0.2*np.tanh(xd))
        shaped = np.tanh(1.5*y0)
    y = shaped
    if model=='rat':
        y = filt(y, biquad('lp',SR,700+9000*tonev**2,0.7)); y = filt(y, biquad('peak',SR,900,0.7,mid+1.5)); y = filt(y, biquad('loshelf',SR,120,0.7071,1.0))
    elif model=='ds1':
        y = filt(y, biquad('lp',SR,2500+6500*tonev,0.6)); y = filt(y, biquad('peak',SR,520,0.7,mid-4.0)); y = filt(y, biquad('loshelf',SR,140,0.7071,2.5-3.0*tonev))
    else:
        y = filt(y, biquad('lp',SR,2000+7000*tonev,0.7)); y = filt(y, biquad('peak',SR,750,0.8,mid)); y = filt(y, biquad('loshelf',SR,120,0.7071,2.0))
    trim = {'generic':0.0,'rat':1.0,'ds1':1.0}[model]
    return y*db2g(level+trim), s

# ---------------- fuzz ----------------
def fuzz(fuzzv=0.7, tonev=0.5, level=-16.0, bias=0.2, amp=0.3):
    x, s = tone(amp=amp)
    pre = filt(x, biquad('hp', SR, 90.0, 0.7))
    g = db2g(20+40*fuzzv)
    bs = 0.05+0.6*bias
    dc = 1-np.exp(-bs)
    xd = pre*g + bs
    shaped = np.where(xd>0, 1-np.exp(-xd), -0.75*(1-np.exp(1.4*xd))) - dc
    y = filt(shaped, biquad('lp',SR,1200+6000*tonev,0.7))
    y = filt(y, biquad('peak',SR,900,0.7,-6.0*(1-tonev)))
    return y*db2g(level), s

# ---------------- Gervana Wuyin ----------------
def wuyin(sustain=0.6, tonev=0.5, balance=0.6, voice=1, mode=0, filter=False):
    x, s = tone(amp=0.3)
    inHp = biquad('hp', SR, 170.0 if filter else 35.0, 0.7)
    inLp = biquad('lp', SR, 2500+14000*balance, 0.6)
    y = filt(filt(x, inHp), inLp)
    inGain = 0.55+0.9*balance
    trim = [0.0,2.5,4.0][voice]
    g = db2g(16+30*sustain+trim)
    dyn = 1.0
    if mode == 1:
        env = 0.0; a = 1-np.exp(-1/(SR*0.002)); r = 1-np.exp(-1/(SR*0.140)); arr=[]
        for v in y:
            av = abs(v); env += (a if av>env else r)*(av-env); arr.append(env)
        env = np.array(arr)
        dyn = 0.25+0.75*np.clip(env/0.12,0,1)
    y = y*inGain*g*dyn
    # shapeOs: tanh(0.35x) -> interHp 60/90, interLp 6000 -> *6 -> asymmetric
    y1 = np.tanh(0.35*y)
    y1 = filt(y1, biquad('hp', 2*SR, 60.0 if voice==0 else 90.0, 0.6))
    y1 = filt(y1, biquad('lp', 2*SR, 6000.0, 0.6))*6.0
    y1 = np.where(y1>=0, np.tanh(y1), np.tanh(1.25*y1)/1.25)
    # post tone mix
    fl = [320,460,680][voice]; fh = [950,1150,1500][voice]
    t = tonev
    y = (1-t)*filt(y1, biquad('lp',SR,fl,0.6))*1.4 + t*filt(y1, biquad('hp',SR,fh,0.6))*1.1
    y = filt(y, biquad('loshelf',SR,120,0.7071, (3.0 if voice==0 else 1.0)))
    y = filt(y, biquad('peak',SR,1600,0.9,0.0))
    y = filt(y, biquad('lp',SR,8000,0.6))
    return y*db2g(-10+1), s

# ---------------- Bingzi ----------------
def bingzi(gainv=0.4, tonev=0.5, tight=140.0, style=0, clip=0, level=-2.0, blend=1.0, amp=0.3):
    x, s = tone(amp=amp)
    y = filt(x, biquad('hp',SR,tight,0.6))*db2g(4+50*gainv)
    xd = y
    if clip==0: sh = np.tanh(xd)
    elif clip==1: sh = np.where(xd>=0, np.tanh(xd), np.tanh(1.7*xd)/1.7)
    else: sh = 1.8*np.tanh(xd/1.8)
    sh = 0.82*sh + 0.18*xd/(1+0.35*np.abs(xd))
    if style==0:
        sh = filt(sh, biquad('peak',SR,900,0.8,4.0)); sh = filt(sh, biquad('lp',SR,900+4800*tonev**2,0.6)); sh = filt(sh, biquad('hishelf',SR,3000,0.7071,0.0))
    elif style==1:
        sh = filt(sh, biquad('peak',SR,1800,0.9,4.0)); sh = filt(sh, biquad('lp',SR,1400+6000*tonev**2,0.7)); sh = filt(sh, biquad('hishelf',SR,3000,0.7071,1.5))
    else:
        sh = filt(sh, biquad('peak',SR,700,0.7,0.5)); sh = filt(sh, biquad('lp',SR,9000,0.6)); sh = filt(sh, biquad('hishelf',SR,2200,0.7071,(tonev-0.6)*14.0))
    trim = {0:0.0,1:-1.0,2:-3.0}[clip]
    wetG = db2g(level-4+trim)
    cleanG = db2g(level+10*gainv)
    out = blend*sh*wetG + (1-blend)*x*cleanG
    return out, s

if __name__ == '__main__':
    print("DC offset after the effect (220 Hz sine, 0.3 peak). DC/peak in dB re output peak.\n")
    print("-- overdrive, drive=18 dB, tone=0.5, bass=0, level=-6 --")
    for m in ['ts808','ts9','klon','sd1','blues','generic']:
        report(m, *overdrive(m))
    print("\n-- distortion, gain=34 dB, tone=0.55, mid=-3 --")
    for m in ['generic','rat','ds1']:
        report(m, *distortion(m))
    print("\n-- fuzz --")
    report('fuzz', *fuzz())
    print("\n-- Gervana Wuyin --")
    report('wuyin (mode0)', *wuyin(mode=0))
    report('wuyin (mode1)', *wuyin(mode=1))
    print("\n-- Gervana Bingzi --")
    for st in range(3):
        report(f'bingzi style{st}', *bingzi(style=st))
    print("\n-- low E (82.4 Hz) for reference --")
    for m in ['sd1','generic']:
        x, s = tone(f=82.41, amp=0.3)
        # reuse function with a swapped tone by monkey-ish: just run with 220; skip
    print("\nPost-filter DC gain check (shelf at DC):")
    for db in (-6, 0, 4, 12):
        b,a = biquad('loshelf', SR, 200, 0.7071, db)
        w = np.array([1.0,0.0]); # evaluate H(z=1) = sum b / sum a
        print(f"  lowshelf {db:+.0f} dB at 200 Hz -> DC gain {20*np.log10(abs(np.sum(b)/np.sum(a))):+.2f} dB")
