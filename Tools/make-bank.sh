#!/bin/sh
# Convertit Assets/samples/<CATÉGORIE>/*.wav en Assets/bank/<CAT>__<Nom>.flac (banque d'usine intégrée au plugin).
set -eu
cd "$(dirname "$0")/../Assets"
rm -rf bank && mkdir bank
for dir in "SUB BASS:SUB" "REESE BASS:REESE" "808:808" "PLUCK BASS:PLUCK"; do
  src=${dir%%:*}; tag=${dir##*:}
  for f in "samples/$src"/*.wav; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in ._*) continue;; esac
    ffmpeg -v error -y -i "$f" -af aresample=osf=s16:dither_method=triangular -c:a flac -compression_level 8 -sample_fmt s16 "bank/${tag}__$(basename "${f%.wav}").flac"
  done
done
find bank -name '._*' -delete
ls bank | wc -l; du -sh bank
