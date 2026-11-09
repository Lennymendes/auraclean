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
    ├── LeftoverScanner.h/.cpp  Deep Clean : fichiers résiduels + reçus .pkg (lsbom)
    ├── DeepCleanDialog.h/.cpp  Modale arborescente : cases, badges, chemins, tailles, total
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

## Deep Clean (désinstallation complète)

« Désinstaller » ouvre une modale listant, par plugin, les bundles ET les fichiers résiduels :
`Application Support`, `Audio/Presets`, `Preferences` (+ `ByHost`), `Caches`, `Logs` (dans `/Library` et
`~/Library`), `/Users/Shared`, `~/Documents/VST3 Presets`, `~/Music/Audio Music Apps/Plug-In Settings`,
`~/Documents/<Vendor>` et `~/Music/<Vendor>` (FabFilter, iZotope, NI, Xfer…), et les reçus `/var/db/receipts/*.bom|.plist` (contenu lu via `lsbom -fls`).

Règles de sécurité :
- **Formats complets** : les résidus ne sont recherchés (et cochables) que si **tous** les formats installés du
  plugin sont cochés. Décocher un bundle dans la modale verrouille ses résidus.
- **Anti-collision marque** : jamais `<Racine>/<Vendor>`, uniquement `<Vendor>/<Plugin>` ou
  `<Vendor>/<Presets|User Presets|Plug-In Settings|Settings|Data>/<Plugin>`. Ensuite, le sous-dossier intermédiaire
  puis `<Vendor>` sont supprimés **seulement** s'ils sont vides (ou ne contiennent que `.DS_Store`), du plus profond
  au plus haut. Revérifié dans `UninstallerMac.mm`.
- `~/Documents` et `~/Music` : **uniquement** sous un dossier `<Vendor>`, jamais au premier niveau (badge PRESETS).
- **Reçus .pkg** : retenus uniquement s'ils référencent un bundle supprimé et aucun plugin encore installé.
  Un dossier déduit du `.bom` n'est retenu que si son nom correspond au plugin ou si **tous** ses fichiers
  proviennent de ce `.bom` ; les noms génériques (`Shared`, `Presets`…) et partagés (Avid, PACE, iLok…) sont exclus.
- Presets utilisateur (`~/Documents/…`, `~/Music/…`, réglages Logic, `~/Library/Audio/Presets`) : **décochés par défaut**.
- Bundles + résidus + élagage des dossiers `<Vendor>` vides passent dans **un seul** lot administrateur.

## Télécharger l'app (GitHub Actions)

Chaque push sur `main` compile une app Universal (arm64 + x86_64) sur un runner macOS.

- **Build ponctuel** : onglet *Actions* → dernier run « Build macOS » → section *Artifacts* → `AuraClean-macOS`
  (contient `AuraClean-macOS.dmg` et `AuraClean-macOS.zip`).
- **Release publique** : *Actions* → « Build macOS » → *Run workflow* → renseigner `release_tag` (ex. `v1.0.0`).
  La Release apparaît ensuite dans l'onglet *Releases* avec le `.dmg` et le `.zip`. Pousser un tag `v*` fait la même chose.

L'app est signée **ad-hoc** (pas de certificat Developer ID dans la CI) : au premier lancement, macOS la bloque.
Clic droit sur AuraClean.app → *Ouvrir*, ou bien :

```bash
xattr -cr /Applications/AuraClean.app
```

## Build

```bash
cmake -B build -G Xcode -DAURASUITE_JUCE_DIR=/chemin/vers/JUCE
cmake --build build --config Release
```

Signature : Hardened Runtime + `com.apple.security.automation.apple-events`. App **non sandboxée**
(obligatoire pour écrire dans `/Library/Audio/Plug-Ins`). Distribution hors App Store, notarisation Developer ID.

## Ergonomie

- **Intro** (~600 ms, 60 FPS) au lancement et à chaque « Rescanner » : titre *Aura Clean* + barre fine
  cyan → violet, puis la liste apparaît en fondu décalé ligne par ligne. Rescan silencieux après une opération.
- **Modale de désinstallation** : overlay `#D007080B` sur l'interface (fondu 150 ms entrée / 120 ms sortie),
  carte `#11131A`, bordure `#252836`, ombre portée.
- **Tailles** : une seule fonction `formatBytes` (base 1024) — `512 o`, `763,3 Ko`, `508,0 Mo`, `1,54 Go`.
- **Colonne Taille** : binaires + ligne « + X données » (presets / données détectés en arrière-plan, hors
  `~/Documents` et reçus .pkg pour ne déclencher aucune autorisation macOS au lancement) ; détail en infobulle.
- **Infobulle par badge** `[VST3]` `[AU]` `[AAX]` : chemin exact, taille, version, architecture, Bundle ID.
- **Clic droit** : afficher chaque format dans le Finder, copier le Bundle ID, copier les chemins,
  cocher / décocher, « Désinstaller ce plugin… ».

## Raccourcis

| Raccourci | Action |
|---|---|
| ⌘F | Rechercher |
| ⌘A | Tout cocher (éléments visibles) |
| Échap | Tout décocher · fermer la modale · vider la recherche |
| ⌘R | Rescanner |
| ⌫ / Suppr | Ouvrir la modale de désinstallation de la sélection |
| Entrée | Cocher les lignes sélectionnées (modale : confirmer) |
| Double-clic | Afficher dans le Finder |
