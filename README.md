# AuraClean — gestionnaire & désinstalleur de plugins (AuraSuite / NextDSP)

Application **Standalone macOS** (JUCE 8, C++20 + Objective-C++), Universal arm64/x86_64, macOS 11+.

## Arborescence

```
AuraClean/
├── CMakeLists.txt            JUCE local (-DAURASUITE_JUCE_DIR=…) ou FetchContent 8.0.4
└── Source/
    ├── Main.cpp              JUCEApplication, fenêtre, refus de quitter pendant une opération
    ├── MachOInspector.h      Lecture des 4 premiers Ko : thin / fat / fat64 → ARM64, x86_64…
    ├── PluginScanner.h/.cpp  Scan passif multi-thread, Info.plist (CF), moduleinfo.json, catalogue
    ├── UninstallerMac.h/.mm  Corbeille / quarantaine, élévation groupée, garde-fous de chemins
    ├── AuraLookAndFeel.h     Thème #0D0E12, cyan / violet, chips, header de table, scrollbars
    └── MainComponent.h/.cpp  Library (filtres + sélection), VendorSidebar, PluginTable, overlay
```

## Flux de données

1. `PluginScanner::scan()` (thread de fond) liste les bundles des 5 dossiers actifs + leurs 5 dossiers
   de quarantaine, puis répartit `inspectBundle()` sur 2–8 threads. Aucun binaire n'est chargé.
2. `inspectBundle()` : Info.plist via `CFPropertyListCreateWithData` (XML **et** binaire), moduleinfo.json
   (commentaires / virgules JSON5 tolérés), taille via `fts(3)`, date = « Date d'ajout » Finder
   (`ATTR_CMN_ADDEDTIME`, repli `st_birthtime`), architecture via `MachOInspector`.
3. `buildCatalogue()` résout la marque par niveaux de confiance :
   nom AU `"Vendor: Plugin"` (100) › moduleinfo (90) › code fabricant AU 4CC **appris** sur les AU
   bien identifiés (80) › propagation par domaine de `CFBundleIdentifier` (75) › bundle id (60) ›
   copyright / GetInfoString (45/40). Nettoyage des suffixes (Inc., GmbH…) et table d'alias
   (`uaudio` → Universal Audio). Regroupement par clé normalisée `marque|nom`.
4. `Library` (message thread) applique filtres, recherche multi-mots, tri, sélection **par bundle**
   (une ligne = un plugin, un badge = un format cliquable).

## Désinstallation / quarantaine

- Tentative sans droits (`trashItemAtURL`, `rename(2)`), puis **un seul** `do shell script … with
  administrator privileges` pour tout le lot restant (une seule demande de mot de passe, au nom d'AuraClean).
- Corbeille des plugins système : root déplace dans `~/Library/Caches/com.nextdsp.auraclean/Staging`
  + `chown` utilisateur, puis l'app envoie à la Corbeille avec ses droits (pas d'accès TCC à `~/.Trash`).
- Quarantaine : `VST3 (Disabled)`, `Components (Disabled)`, et `Plug-Ins (Unused)` pour AAX
  (convention native de Pro Tools). Réactivation = déplacement inverse, sans écraser.
- Garde-fous : extension obligatoire, pas de `..` ni de caractères de contrôle, parent résolu
  (`realpath`) dans un dossier autorisé, destination = dossier connu, quoting POSIX + AppleScript.
- Après toute action sur des AU : `killall -9 AudioComponentRegistrar` (cache AU reconstruit).

## Build

```bash
cmake -B build -G Xcode -DAURASUITE_JUCE_DIR=/chemin/vers/JUCE
cmake --build build --config Release
```

Signature : Hardened Runtime + `com.apple.security.automation.apple-events`. App **non sandboxée**
(obligatoire pour écrire dans `/Library/Audio/Plug-Ins`). Distribution hors App Store, notarisation Developer ID.

## Raccourcis

⌘F recherche · ⌘R rescanner · ⌘A tout cocher · Entrée coche les lignes sélectionnées ·
double-clic = Finder · clic droit (ligne ou marque) = menu contextuel.
