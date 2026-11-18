"""Fabrique Assets/samples à partir des fichiers d'origine, sans toucher à leur son.

Usage : python3 -I Tools/build-samples.py
- Source : Assets/samples-originaux (non publié), noms Swell dans Tools/noms.json, hauteurs mesurées dans
  Tools/pitch-originaux.json.
- Seul le volume change (crête commune à -1 dB, calcul en flottant : rien n'est écrêté). Aucun rééchantillonnage :
  la hauteur exacte est écrite dans le nom (ex. « Maree_E1+15 » = mi + 15 cents) et le plugin transpose une seule
  fois, au moment de jouer.
- Puis Tools/extend.py prolonge la queue des sons qui s'arrêtent trop tôt.
"""
import importlib.util, json, os, subprocess, sys, tempfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ORIG = os.path.join(ROOT, "Assets", "samples-originaux")
OUT = os.path.join(ROOT, "Assets", "samples")
NOTES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

spec = importlib.util.spec_from_file_location("extend", os.path.join(HERE, "extend.py"))
extend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extend)

names = json.load(open(os.path.join(HERE, "noms.json"), encoding="utf-8"))
pitch = {r["file"][2:]: r["midi"] for r in json.load(open(os.path.join(HERE, "pitch-originaux.json"), encoding="utf-8"))}

for cat in ("SUB BASS", "REESE BASS", "808", "PLUCK BASS"):
    d = os.path.join(OUT, cat)
    os.makedirs(d, exist_ok=True)
    for f in os.listdir(d):
        if f.lower().endswith(".wav"):
            try:
                os.remove(os.path.join(d, f))
            except FileNotFoundError:
                pass   # fichiers « ._ » du disque, déjà partis avec leur fichier

tmp = tempfile.mkdtemp()
for rel, name in names.items():
    cat = os.path.dirname(rel)
    midi = pitch[rel]
    r = int(round(midi))
    cents = int(round((midi - r) * 100))
    tag = f"{NOTES[r % 12]}{r // 12 - 1}{cents:+d}"
    x, sr = extend.load(os.path.join(ORIG, rel))
    x = x * (10 ** (-1 / 20) / np.abs(x).max())
    t = os.path.join(tmp, "src.wav")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f64le", "-ar", str(sr), "-ac", "2", "-i", "-", "-c:a", "pcm_f32le", t],
                   input=np.ascontiguousarray(x).tobytes(), check=True)
    out = os.path.join(OUT, cat, f"{name}_{tag}.wav")
    res = extend.process(t, out, cat, 440.0 * 2 ** ((midi - 69) / 12))
    print("\t".join([cat, os.path.basename(out)] + [("%.3f" % v) if isinstance(v, float) else str(v) for v in res]), flush=True)
