"""Contrôle des samples prolongés : la queue ne doit jamais remonter, et le début doit rester au niveau de l'original.
Usage : python3 -I Tools/check-samples.py"""
import glob, json, os, subprocess
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
names = json.load(open(os.path.join(HERE, "noms.json"), encoding="utf-8"))
def ld(p):
    raw = subprocess.run(["ffmpeg", "-v", "quiet", "-i", p, "-ar", "44100", "-f", "f64le", "-"], capture_output=True).stdout
    ch = int(subprocess.run(["ffprobe", "-v", "quiet", "-select_streams", "a:0", "-show_entries", "stream=channels", "-of", "csv=p=0", p], capture_output=True, text=True).stdout)
    a = np.frombuffer(raw, np.float64).reshape(-1, ch)
    return a[:, 0], np.abs(a).max()                                   # canal gauche + crête tous canaux
bad = 0
for rel, name in names.items():
    o, opk = ld(os.path.join(ROOT, "Assets", "samples-originaux", rel)); o = o * (10 ** (-1 / 20) / opk)
    p = glob.glob(os.path.join(ROOT, "Assets", "samples", os.path.dirname(rel), name + "_*.wav"))[0]; x, _ = ld(p)
    n = min(len(o), int(0.25 * 44100))                                # 0,25 s : avant tout raccord
    head = 20 * np.log10(np.sqrt((x[:n] ** 2).mean()) / np.sqrt((o[:n] ** 2).mean()))          # début vs original
    w = int(0.5 * 44100)
    env = np.array([np.sqrt((x[i:i + w] ** 2).mean()) for i in range(0, len(x) - w, w)]) + 1e-12
    db = 20 * np.log10(env / env.max())
    body = db[: max(1, min(len(db), int(len(o) / w)))].max()
    rise = (db[len(o) // w:].max() - body) if len(db) > len(o) // w else -99                     # queue au-dessus du corps ?
    flag = abs(head) > 0.5 or rise > 1.0
    bad += flag
    if flag: print(f"PROBLÈME {os.path.basename(p):26} début {head:+.1f} dB vs original, queue {rise:+.1f} dB au-dessus du corps")
print(f"{len(names) - bad}/{len(names)} samples corrects")
