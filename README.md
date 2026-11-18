# Swell Bass

Instrument de basse par Swell Audio : SUB BASS, REESE BASS, 808 et PLUCK BASS. Banque d'usine intégrée au plugin, plus un dossier « Mes samples » pour ajouter ses propres sons.

Formats : AU, VST3 et Standalone (macOS universel), VST3 et Standalone (Windows).

## Compiler (macOS)

```bash
./build-mac.sh
```

Le script génère la banque FLAC (`Tools/make-bank.sh`) à partir de `Assets/samples`, puis compile. Résultats dans `build/SwellBass_artefacts/Release/`.

## Organisation

| Dossier | Contenu |
|---|---|
| `Source/PluginProcessor.*` | Moteur mono (glide, legato, punch, detune, snap), paramètres, licence |
| `Source/PluginEditor.*` | Interface |
| `Source/Bank.*` | Banque d'usine + dossier utilisateur |
| `Source/Licence/` | Module de licence commun Swell |
| `Assets/samples/` | Samples sources, accordés sur C, un dossier par catégorie |
| `Assets/bank/` | Banque FLAC intégrée (générée) |
| `Tools/BassTool.cpp` | Outil hors ligne : rendus audio et captures (`-DBASS_BUILD_TOOLS=ON`) |
