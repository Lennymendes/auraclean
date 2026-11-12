# AuraClean — gestionnaire & désinstalleur de plugins (AuraSuite / NextDSP)

Application **Standalone macOS** (JUCE 8, C++20 + Objective-C++), Universal arm64/x86_64, macOS 11+.

## Arborescence

```
AuraClean/
├── CMakeLists.txt            JUCE local (-DAURASUITE_JUCE_DIR=…) ou FetchContent 8.0.4
├── Resources/                AuraCleanLogo.png (interface), AuraClean-icon.png (icône macOS, grille 824/1024)
└── Source/
    ├── Main.cpp              JUCEApplication, fenêtre, refus de quitter pendant une opération
    ├── MachOInspector.h      Lecture des 4 premiers Ko : thin / fat / fat64 → ARM64, x86_64…
    ├── PluginScanner.h/.cpp  Scan passif multi-thread, Info.plist (CF), moduleinfo.json, catalogue
    ├── UninstallerMac.h/.mm  Corbeille / quarantaine, élévation groupée, garde-fous de chemins
    ├── LeftoverScanner.h/.cpp  Deep Clean : fichiers résiduels + reçus .pkg (lsbom)
    ├── DeepCleanDialog.h/.cpp  Modale arborescente : cases, badges, chemins, tailles, total
    ├── UpdateChecker.h/.cpp    Vérification de mise à jour (thread, JSON, comparaison de versions)
    ├── SettingsDialog.h/.cpp   Réglages (engrenage, ⌘,) : onglets Licence / Mises à jour / À propos
    ├── LicenseManager.h/.cpp   Licence Lemon Squeezy (activation, désactivation, fichier signé hors-ligne)
    ├── LicenseDialog.h/.cpp    Modale « Activer AuraClean »
    ├── AuraWidgets.h           IconButton (engrenage, fermer), LogoButton (logo cliquable → site)
    ├── AppInfo.h               Site, crédits, logo embarqué (BinaryData)
    ├── AuraLookAndFeel.h     Thème gris neutre, chips, header de table, scrollbars
    └── MainComponent.h/.cpp  Library (filtres + sélection), VendorSidebar, PluginTable, overlay
```

## Flux de données

1. `PluginScanner::scan()` (thread de fond) liste les bundles de 5 formats — **VST3, AU, AAX, VST (VST2 `.vst`)
   et CLAP (`.clap`)** — dans `/Library/Audio/Plug-Ins/{VST3,Components,VST,CLAP}`, leurs équivalents `~/Library`,
   le dossier Avid, et les dossiers de quarantaine `… (Disabled)` correspondants, puis répartit `inspectBundle()` sur 2–8 threads. Aucun binaire n'est chargé.
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
`~/Documents/<Vendor>` et `~/Music/<Vendor>` (FabFilter, iZotope, NI, Xfer…), `~/Library/{Saved Application State,
HTTPStorages,WebKit,Application Scripts}`, `/Library/Audio/{Impulse Responses,Sounds}/<Vendor>`,
`/Library/Application Support/Avid/Audio/Plug-In Settings`, et les reçus `/var/db/receipts/*.bom|.plist` (contenu lu via `lsbom -fls`).

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

## Mises à jour

Au lancement (après 1,5 s, en silence) et via **AuraClean ▸ Rechercher des mises à jour…** (ou **Aide**, ou
**À propos d'AuraClean…**), `UpdateChecker` (un `juce::Thread`) fait un `GET` sur
`https://raw.githubusercontent.com/Lennymendes/auraclean-updates/main/version.json` (dépôt public
`Lennymendes/auraclean-updates`) — modèle : [`docs/version.json`](docs/version.json).
Le CDN de GitHub met ce fichier en cache quelques minutes après chaque push.

```json
{ "latest_version": "1.0.1", "download_url": "https://nextdsp.com/downloads/AuraClean-1.0.1.dmg",
  "release_notes": "Correction de bugs" }
```

- Comparaison SemVer avec la version de l'app (`1.0.1 > 1.0.0`, `1.10 > 1.9`, `1.1.0-beta.2 < 1.1.0`).
- **Téléchargement intégré** : si `download_url` pointe vers un `.dmg` (ex.
  `https://github.com/Lennymendes/auraclean-updates/releases/latest/download/AuraClean-macOS.dmg`), l'app le télécharge
  elle-même dans `~/Downloads` (progression dans la bannière) puis ouvre l'image disque. Sinon, navigateur.
- Nouvelle version → bannière en haut de la fenêtre : *Télécharger la mise à jour* (navigateur),
  *Plus tard*, *Ignorer cette version* (mémorisé dans `~/Library/Application Support/NextDSP/AuraClean.settings`).
- Au lancement, aucune erreur réseau n'est affichée ; depuis le menu, « à jour » ou l'erreur s'affiche.
- Sécurité : HTTPS obligatoire, réponse ≤ 64 Ko, `download_url` limité aux domaines de
  `UpdateConfig::allowedDownloadHosts()` : `nextdsp.com`, `lemonsqueezy.com`, `paddle.com`, `github.com`,
  `raw.githubusercontent.com` (et leurs sous-domaines).
- **Publier une version** : incrémenter `project(AuraClean VERSION x.y.z)` dans `CMakeLists.txt`, construire,
  puis modifier `latest_version`, `download_url` et `release_notes` dans `version.json` du dépôt
  `auraclean-updates` et pousser.

## Licence (Lemon Squeezy) — « Scan gratuit / Nettoyage sous licence »

- **Mode Aperçu** (sans licence) : scan, filtres, recherche, sélection et modale *Prévisualisation de
  désinstallation* (fichiers résiduels + espace récupérable). Le bouton final (Corbeille, Désactiver,
  Réactiver) ouvre **Activer AuraClean** ; après activation, l'action demandée repart automatiquement.
- **Activation** : `POST https://api.lemonsqueezy.com/v1/licenses/activate` (`license_key`, `instance_name` =
  nom du Mac), sur un thread dédié. Acceptée seulement si `activated == true`, statut ni `expired` ni `disabled`,
  **et** `meta.product_name` contient « AuraClean » (une clé d'un autre produit est refusée et son emplacement
  aussitôt libéré). Messages FR : clé introuvable, expirée, désactivée, autre produit, limite de 3 Mac, réseau.
- **Hors-ligne** : `~/Library/Application Support/NextDSP/AuraClean/license.dat` (clé, `instance.id`,
  `meta.product_id`) signé SHA-256 avec l'`IOPlatformUUID` du Mac (IOKit) + sel interne. Vérifié localement à
  chaque lancement ; copié sur un autre Mac ou modifié → refusé.
- **Désactiver ce Mac** (Réglages ▸ Licence) : `POST /v1/licenses/deactivate` (`license_key`, `instance_id`)
  puis suppression du fichier local.
- Configuration : `LicenseConfig` dans `LicenseManager.h` (Checkout URL, prix affiché, nom de produit attendu).

## Réglages & À propos

- Bouton **engrenage** de l'en-tête, **⌘,** ou menu **AuraClean ▸ Réglages… / À propos d'AuraClean…**.
- **Licence** : Mode Aperçu (*Activer une licence…*, *Acheter (14,99 €)*) ou badge *Licence Active*, clé masquée
  `XXXX-••••-••••-XXXX`, *Désactiver ce Mac*.
- **Mises à jour** : version installée, état en direct (recherche, à jour, disponible, erreur),
  *Rechercher des mises à jour*, *Télécharger*, interrupteur *Rechercher automatiquement au lancement*.
- **À propos** : logo (clic → nextdsp.com), version, « Développé avec ♥ par ZEYN », bouton du site.
- Le **logo de l'en-tête** est aussi un lien vers le site.

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
