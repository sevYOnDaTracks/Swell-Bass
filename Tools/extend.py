"""Prolonge les samples de basse sans boucle audible.

Usage : python3 -I Tools/extend.py <dossier source> <dossier sortie> [rapport.tsv]
  source : Assets/samples (un sous-dossier par catégorie, WAV accordés)
  sortie : même arborescence, WAV 24 bits prolongés

Méthode
- Si le son est déjà retombé à -45 dB sous sa crête à la fin du fichier : rien à faire (petit fondu final).
- Sons harmoniques (808, sub, pluck) : on mesure, juste avant la fin, la fondamentale exacte, l'amplitude et la
  phase de chaque harmonique et la vitesse de chute. La suite est recréée par synthèse additive en continuité de
  phase, raccordée en fondu, et s'éteint naturellement (les harmoniques aigus plus vite que le grave). Aucun motif
  ne se répète : pas de pulsation.
- Sons qui bougent (Reese, ou si l'analyse harmonique explique mal le son) : on enchaîne des passages différents
  du son lui-même avec de longs fondus à puissance constante, choisis là où ils se ressemblent le plus, en suivant
  la chute de volume.
"""
import os, subprocess, sys
import numpy as np

SR_OUT = None
MAX_EXT = 12.0        # secondes ajoutées au maximum
T60_LONG = 14.0       # 808 / sub / reese : la queue met au moins ~14 s pour perdre 60 dB
T60_PLUCK = 7.0       # pluck : un peu plus courte (le bouton Decay raccourcit au besoin)
FLOOR_DB = -45.0      # au-dessous, le fichier finit déjà naturellement


def load(path):
    info = subprocess.run(["ffprobe", "-v", "quiet", "-select_streams", "a:0", "-show_entries", "stream=sample_rate,channels",
                           "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip().split(",")
    sr, ch = int(info[0]), int(info[1])
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", path, "-f", "f64le", "-"], capture_output=True).stdout
    x = np.frombuffer(raw, np.float64).reshape(-1, ch)
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    return x[:, :2].copy(), sr


def save(path, x, sr):
    p = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f64le", "-ar", str(sr), "-ac", "2", "-i", "-", "-c:a", "pcm_s24le", path],
                       input=np.ascontiguousarray(x, np.float64).tobytes())
    if p.returncode:
        raise RuntimeError("écriture impossible : " + path)


def envelope(m, sr, win=0.03, hop=0.005):
    h, w = int(hop * sr), int(win * sr)
    sq = np.concatenate([[0.0], np.cumsum(m * m)])
    idx = np.arange(0, max(1, len(m) - w), h)
    return np.sqrt((sq[idx + w] - sq[idx]) / w) + 1e-12, h


def join_point(env, h, sr, n):
    """Dernier endroit fiable avant la fin : on écarte un éventuel fondu ou une coupure dans les derniers instants."""
    db = 20 * np.log10(env)
    step = int(0.05 * sr / h)
    end = len(db) - 1 - int(0.02 * sr / h)
    rates = db[step:] - db[:-step]                      # dB par 50 ms
    lo = max(0, end - int(1.5 * sr / h))
    ref = np.median(rates[lo:max(lo + 1, end - step)]) if end - step > lo else 0.0
    i, calm = end, 0
    while i > step and i > end - int(0.8 * sr / h):
        if rates[i - step] > ref - 2.0:
            calm += 1
            if calm >= 4:
                break
        else:
            calm = 0
        i -= 1
    return min(n - 1, (i + calm) * h)


def estimate_f0(m, sr):
    seg = (m - m.mean()) * np.hanning(len(m))
    nfft = 1 << 20
    sp = np.abs(np.fft.rfft(seg, nfft))
    f = np.fft.rfftfreq(nfft, 1 / sr)
    hps = sp.copy()
    for k in (2, 3):
        d = sp[::k]
        hps[:len(d)] *= d
    lo, hi = np.searchsorted(f, 25), np.searchsorted(f, 500)
    i = lo + int(np.argmax(hps[lo:hi]))
    return f[i]


def harmonic_fit(seg, sr, f0, decay, fmax=1500.0):
    """Moindres carrés : seg(τ) ≈ e^{-decay·τ} Σ (c_k cos + s_k sin)(2π k f0 τ), τ ≤ 0 (fin au point de raccord)."""
    n = len(seg)
    tau = (np.arange(n) - (n - 1)) / sr
    K = max(1, int(min(fmax, 0.45 * sr) / f0))
    w = 2 * np.pi * f0 * np.outer(tau, np.arange(1, K + 1))
    A = np.hstack([np.cos(w), np.sin(w)]) * np.exp(-decay * tau)[:, None]
    coef = np.linalg.lstsq(A, seg, rcond=None)[0]
    resid = seg - A @ coef
    r2 = 1 - (resid ** 2).sum() / max(1e-20, (seg ** 2).sum())
    return coef[:K], coef[K:], r2


def refine_f0(seg, sr, f0, decay):
    best = (-1, f0)
    for f in f0 * (1 + np.linspace(-0.012, 0.012, 49)):
        r2 = harmonic_fit(seg, sr, f, decay)[2]
        if r2 > best[0]:
            best = (r2, f)
    lo = best[1]
    for f in lo * (1 + np.linspace(-0.0006, 0.0006, 25)):
        r2 = harmonic_fit(seg, sr, f, decay)[2]
        if r2 > best[0]:
            best = (r2, f)
    return best[1], best[0]


def extend_harmonic(x, sr, j, env, h, category, f_expected=None):
    m = x.mean(axis=1)
    fit_len = int(0.16 * sr)
    a0 = max(0, j - fit_len)
    seg_m = m[a0:j]
    # vitesse de chute mesurée sur la dernière demi-seconde fiable (en 1/s)
    e0, e1 = max(0, (j - int(0.5 * sr)) // h), max(1, j // h)
    tt = np.arange(e0, e1) * h / sr
    slope_db = np.polyfit(tt, 20 * np.log10(env[e0:e1]), 1)[0] if e1 - e0 > 4 else -6.0
    decay_meas = max(0.0, -slope_db / 8.686)
    f0 = estimate_f0(m[max(0, j - int(0.3 * sr)):j], sr)
    if f_expected:
        # la hauteur est connue : on ne garde que l'octave, pour ne jamais partir sur une fausse fondamentale
        f0 = f_expected * 2 ** round(np.log2(f0 / f_expected))
    f0, r2 = refine_f0(seg_m, sr, f0, decay_meas)
    f_low, r2_low = refine_f0(seg_m, sr, f0 / 2, decay_meas)
    if r2_low > r2 + 0.002:                            # une composante à l'octave en dessous existe : on la garde
        f0, r2 = f_low, r2_low
    if r2 < 0.93:
        return None, r2, f0
    # chute de la queue : naturelle, mais jamais plus rapide qu'une queue longue pour 808 / sub
    a_ext = min(decay_meas, np.log(1000) / (T60_PLUCK if category == "PLUCK BASS" else T60_LONG))
    FMAX = 10000.0
    K = max(1, int(min(FMAX, 0.45 * sr) / f0))
    X = int(min(0.09, 6 / f0) * sr)                     # fondu de raccord : au moins quelques périodes
    ext = int(MAX_EXT * sr)
    tau = (np.arange(-X, ext)) / sr
    out = np.zeros((X + ext, 2))
    k = np.arange(1, K + 1)
    a_k = a_ext * (1 + 0.12 * (k - 1))                  # les aigus s'éteignent plus vite : la queue s'arrondit
    for c in range(2):
        cs, ss, _ = harmonic_fit(x[a0:j, c], sr, f0, decay_meas, FMAX)
        # on ne garde que les vraies harmoniques : celles à moins de 60 dB sous la plus forte (le reste = bruit de fond)
        amp = np.hypot(cs, ss)
        keep = amp >= amp.max() * 1e-3
        cs, ss = cs * keep, ss * keep
        y = np.zeros(len(tau))
        for k0 in range(0, K, 16):                      # par paquets d'harmoniques (mémoire)
            kk = np.arange(k0, min(K, k0 + 16))
            ph = 2 * np.pi * f0 * np.outer(tau, kk + 1)
            g = np.where(tau[:, None] < 0, np.exp(-decay_meas * tau)[:, None], np.exp(-np.outer(tau, a_k[kk])))
            y += (g * (cs[kk] * np.cos(ph) + ss[kk] * np.sin(ph))).sum(axis=1)
        out[:, c] = y
    # raccord : fondu à gain constant (les deux signaux sont en phase)
    wfade = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, X))
    res = np.concatenate([x[:j - X], np.zeros((ext + X, 2))])
    res[j - X:j] = x[j - X:j] * (1 - wfade)[:, None] + out[:X] * wfade[:, None]
    res[j:] = out[X:]
    return res, r2, f0


def extend_texture(x, sr, j, env, h):
    """Enchaîne des passages du son lui-même avec de longs fondus à puissance constante. Chaque passage est mis
    exactement au niveau du raccord, puis suit la pente du corps du son. Les passages varient pour ne rien répéter."""
    m = x.mean(axis=1)
    # matière utilisable : dès que l'attaque est passée (son à -6 dB de sa crête), pas seulement après la crête
    first_loud = int(np.argmax(env >= env.max() * 0.5))
    pk = first_loud * h
    src_lo = max(0, pk + int(0.3 * sr), j - int(3.0 * sr))
    avail = j - src_lo
    if avail < int(0.8 * sr):
        return None
    seg_len = int(min(2.5 * sr, avail * 0.6))
    xf = int(min(0.6 * sr, seg_len * 0.35))
    # pente du corps du son (robuste), bornée : jamais plus rapide qu'une queue longue
    e0, e1 = src_lo // h, max(src_lo // h + 2, j // h)
    tt = np.arange(e0, e1) * h / sr
    db = 20 * np.log10(env[e0:e1])
    slope_db = np.polyfit(tt, db, 1)[0] if len(tt) > 8 else 0.0
    slope_db = float(np.clip(slope_db, -60.0 / T60_LONG, 0.0))
    rms = lambda a: np.sqrt((a ** 2).mean()) + 1e-12
    out = x[:j].copy()
    target_len = j + int(MAX_EXT * sr)
    rng = np.random.default_rng(11)
    recent = []
    hi = j - seg_len - xf
    if hi <= src_lo:
        return None
    while len(out) < target_len:
        tail = out[-xf:]
        tail_m = tail.mean(axis=1)
        cands = rng.integers(src_lo, hi, 40)
        best, bs = -1e9, int(cands[0])
        for s0 in cands:
            if any(abs(int(s0) - r) < int(0.4 * sr) for r in recent[-3:]):
                continue                                   # pas le même passage que les derniers
            ref = m[s0:s0 + xf]
            corr = np.dot(tail_m, ref) / (np.linalg.norm(tail_m) * np.linalg.norm(ref) + 1e-12)
            lvl = -abs(20 * np.log10(rms(ref) / rms(tail_m)))
            score = corr + 0.05 * lvl
            if score > best:
                best, bs = score, int(s0)
        # alignement à l'échantillon près sur la fin actuelle (±40 ms), par corrélation
        lag_max = int(0.04 * sr)
        lo, hi2 = max(src_lo, bs - lag_max), min(hi, bs + lag_max)
        seg = m[lo:hi2 + xf]
        nfft = 1 << int(np.ceil(np.log2(len(seg) + xf)))
        cc = np.fft.irfft(np.fft.rfft(seg, nfft) * np.conj(np.fft.rfft(tail_m, nfft)), nfft)[:hi2 - lo + 1]
        energy = np.sqrt(np.convolve(seg ** 2, np.ones(xf), "valid")[:hi2 - lo + 1]) + 1e-12
        k = int(np.argmax(cc / energy))
        bs = lo + k
        recent.append(bs)
        piece = x[bs:bs + seg_len + xf].copy()
        piece *= rms(tail) / rms(piece[:xf])               # même niveau au raccord
        tp = np.arange(len(piece)) / sr
        piece *= (10 ** (slope_db * tp / 20))[:, None]     # puis la pente naturelle
        # fondu à puissance constante qui tient compte de la ressemblance ρ des deux passages :
        # a² + b² + 2ρab = 1, avec b/a qui suit une courbe en S
        rho = float(np.clip(np.dot(tail_m, piece[:xf].mean(axis=1)) /
                            (np.linalg.norm(tail_m) * np.linalg.norm(piece[:xf].mean(axis=1)) + 1e-12), -0.2, 1.0))
        t = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, xf))
        a_, b_ = 1 - t, t
        norm = np.sqrt(a_ ** 2 + b_ ** 2 + 2 * rho * a_ * b_)
        out[-xf:] = tail * (a_ / norm)[:, None] + piece[:xf] * (b_ / norm)[:, None]
        out = np.concatenate([out, piece[xf:]])
    return out[:target_len]


def finish(y, sr, peak_env):
    # coupe quand la queue est retombée à -80 dB, puis court fondu
    m = np.abs(y).max(axis=1)
    thr = peak_env * 10 ** (-80 / 20)
    last = len(y) - 1
    above = np.nonzero(m > thr)[0]
    if len(above):
        last = min(len(y) - 1, above[-1] + int(0.02 * sr))
    y = y[:last + 1].copy()
    f = min(len(y), int(0.03 * sr))
    y[-f:] *= np.linspace(1, 0, f)[:, None]
    return y


def settled_point(env, h, sr, n):
    """Pour les sons harmoniques : le moment où le son est posé (attaque et descente de hauteur finies) mais
    encore fort. C'est là qu'on reprend la suite, pour que la queue dure."""
    pk = int(np.argmax(env))
    db = 20 * np.log10(env / env[pk])
    start = pk + int(0.25 * sr / h)
    for i in range(start, len(db)):
        if db[i] <= -9.0:
            return min(n - 1, i * h)
    return None


def process(src, dst, category, f_expected=None):
    x, sr = load(src)
    m = x.mean(axis=1)
    env, h = envelope(m, sr)
    peak = env.max()
    tail_db = 20 * np.log10(env[-max(1, int(0.03 * sr / h)):].mean() / peak)
    j_end = join_point(env, h, sr, len(x))
    y = r2 = f0 = None
    method = ""
    j = j_end
    lvl = lambda i: 20 * np.log10(env[min(len(env) - 1, max(0, i) // h)] / peak)
    if category != "REESE BASS":
        # 1) reprise là où le son est posé, puis on recule le raccord tant que l'analyse n'est pas excellente
        js = settled_point(env, h, sr, len(x))
        candidates = []
        if js is not None:
            candidates += [js + int(k * 0.08 * sr) for k in range(0, 12)]
        candidates.append(j_end)
        for need in (0.985, 0.95):
            for jc in candidates:
                if jc >= j_end + 1 or jc < int(0.2 * sr):
                    continue
                if lvl (jc) < -35:
                    break
                yc, r2c, f0c = extend_harmonic(x, sr, jc, env, h, category, f_expected)
                if yc is not None and r2c >= need:
                    y, r2, f0, j, method = yc, r2c, f0c, jc, "résynthèse"
                    break
            if y is not None:
                break
    if y is None:
        # 2) son qui bouge : on reprend avant le fondu de fin, là où le corps du son est encore à son niveau
        pk = int(np.argmax(env >= peak * 0.5))          # fin de l'attaque (pas la crête, qui peut être tardive)
        body0 = pk + int(0.3 * sr / h)
        jt = j_end
        if body0 < len(env) - 10:
            db = 20 * np.log10(env / peak)
            alive = db[body0:] > -60                        # le silence numérique de fin ne compte pas
            body = np.median(db[body0:][alive]) if alive.any() else -99
            above = np.nonzero(db[body0:] >= body - 6)[0]
            if len(above):
                jt = min(j_end, (body0 + above[-1]) * h - int(0.05 * sr))
        if lvl (jt) > -30:
            yt = extend_texture(x, sr, jt, env, h)
            if yt is not None:
                y, j, method = yt, jt, "fondus longs"
    if y is None:
        save(dst, finish(x, sr, peak), sr)
        return "inchangé", tail_db, r2, f0
    y = finish(y, sr, peak)
    pk = np.abs(y).max()
    if pk > 0.95:
        y *= 0.95 / pk
    save(dst, y, sr)
    return method, tail_db, r2, f0, j / sr, len(y) / sr, lvl (j)


if __name__ == "__main__":
    src_root, dst_root = sys.argv[1], sys.argv[2]
    report = open(sys.argv[3], "w") if len(sys.argv) > 3 else None
    for cat in ("SUB BASS", "REESE BASS", "808", "PLUCK BASS"):
        os.makedirs(os.path.join(dst_root, cat), exist_ok=True)
        for f in sorted(os.listdir(os.path.join(src_root, cat))):
            if not f.lower().endswith(".wav") or f.startswith("._"):
                continue
            r = process(os.path.join(src_root, cat, f), os.path.join(dst_root, cat, f), cat)
            line = "\t".join([cat, f] + [("%.3f" % v) if isinstance(v, float) else str(v) for v in r])
            print(line, flush=True)
            if report:
                report.write(line + "\n")
