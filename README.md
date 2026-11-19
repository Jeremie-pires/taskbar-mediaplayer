# Taskbar Media Player

Un mini-lecteur dans la barre des tâches Windows : pochette, titre, artiste et boutons précédent / lecture / suivant, juste à gauche de la zone de notification. Il pilote n'importe quel lecteur qui s'affiche dans le panneau média de Windows (Spotify, navigateurs, Deezer, VLC, lecteur Windows…).

- **Sans droits admin** : installeur par utilisateur ou `.exe` portable, rien n'est chargé dans Explorer.
- **Windows 10 et 11**, barre des tâches horizontale (en bas ou en haut).
- Suit le thème clair / sombre et la mise à l'échelle (DPI).

## Installation

- **Installeur** : `TaskbarMediaPlayer-Setup-<version>.exe`. Installe dans `%LOCALAPPDATA%\Programs` sans droits admin, propose le démarrage avec Windows, et se désinstalle depuis *Paramètres > Applications* (réglages et démarrage automatique compris).
- **Portable** : `TaskbarMediaPlayer-<version>-portable.zip`, à décompresser où l'on veut.

## Utilisation

1. Lancer `taskbar-mediaplayer.exe`. La bande apparaît dans la barre des tâches, avec une icône ♪ dans la zone de notification.
2. **Clic gauche** sur les boutons : précédent, lecture / pause, suivant. **Molette** : volume.
3. **Clic droit** sur la bande ou sur l'icône ♪ : menu (lecture, *Paramètres…*, *Démarrer avec Windows*, *Quitter*).
4. Relancer l'exe alors qu'il tourne déjà ouvre les paramètres.

### Paramètres

Chaque changement s'applique tout de suite.

| Réglage | Effet |
|---|---|
| Taille | *Compact* : pochette et boutons. *Classique* : ajoute titre et artiste. *Large* : ajoute une barre de progression. La largeur suit celle de la barre des tâches (8 %, 16 % ou 24 %), dans des limites adaptées au DPI. |
| Taille du texte | En pixels à 100 % ; adaptée automatiquement au DPI. |
| Thème | *Barre des tâches* (suit Windows), *Clair*, *Sombre* (fond opaque), *Transparent* (texte blanc). |
| Contenu, boutons | Pochette, titre, artiste, chaque bouton (le bouton paramètres est masqué par défaut). |
| Masquer après une pause de | Cache la bande après ce délai sans lecture. |
| Masquer quand rien ne joue | Cache la bande sans session média. |
| Masquer pendant un plein écran | Cache la bande quand l'application au premier plan couvre l'écran de la barre des tâches. |

La barre de progression du mode *Large* utilise la position que le lecteur publie à Windows : elle n'apparaît que si le lecteur en publie une, et un clic ou un glisser déplace la lecture seulement si le lecteur l'accepte.

Les réglages sont dans `HKCU\Software\TaskbarMediaPlayer` ; ceux de l'ancien nom (`HKCU\Software\MusicLounge`) sont repris au premier lancement, de même que le démarrage avec Windows. *Démarrer avec Windows* ajoute une valeur dans `HKCU\...\CurrentVersion\Run`.

Avec une petite barre des tâches (petites icônes), titre et artiste s'affichent sur une seule ligne. Si une autre application place déjà sa bande à cet endroit (BatteryHub par exemple), Taskbar Media Player se met à sa gauche.

## Compiler

Prérequis : Visual Studio 2022 (charge de travail « Développement desktop en C++ », SDK Windows 10/11) et CMake 3.21+.

```bat
build.bat
```

Résultat : `build\Release\taskbar-mediaplayer.exe`. Pour recompiler pendant que l'appli tourne, la fermer d'abord avec `taskbar-mediaplayer.exe --quit`.

Installeur et zip portable (il faut en plus [Inno Setup](https://jrsoftware.org/isdl.php) 6 ou plus récent) :

```powershell
powershell -ExecutionPolicy Bypass -File installer\build-installer.ps1
```

Résultat dans `dist\` : l'installeur, le zip portable et `SHA256SUMS.txt`. La version est celle de `project(... VERSION ...)` dans `CMakeLists.txt`.

## Intégration continue et versions

- **CI** (`.github/workflows/ci.yml`, à chaque push ou pull request sur `main`) : compilation avec les avertissements traités comme des erreurs, construction de l'installeur, puis analyse [SonarQube Cloud](https://sonarcloud.io) si le secret `SONAR_TOKEN` est défini.
- **Publication** (`.github/workflows/release.yml`) : pousser un tag `vX.Y.Z` (ou `vX.Y.Z-rc.N` pour une préversion) publie une release GitHub avec l'installeur, le zip portable et `SHA256SUMS.txt`. Le tag doit correspondre à la version de `CMakeLists.txt` ; le message d'un tag annoté sert de notes de version.

```bat
git tag -a v2.0.0 -m "Notes de version"
git push origin v2.0.0
```

## Architecture

| Fichier | Rôle |
|---|---|
| `src/main.cpp` | Fenêtre cachée propriétaire de l'appli : tick d'une seconde, menu, icône de notification, `TaskbarCreated` (redémarrage d'Explorer), `--quit`. |
| `src/widget.cpp` | La bande : fenêtre *layered* enfant de `Shell_TrayWnd`, dessinée avec GDI+, placée à gauche de `TrayNotifyWnd`. Sous Windows 10, réduit `ReBarWindow32` pour que les boutons de tâches ne passent pas dessous. |
| `src/media.cpp` | API média de Windows (GSMTC, C++/WinRT) sur un thread dédié ; les commandes visent le lecteur affiché. Le thread de l'interface ne bloque jamais (Explorer l'attend, la bande étant enfant de la barre des tâches). |
| `src/icons.cpp` | Icônes vectorielles ([Phosphor](https://phosphoricons.com), graisse *fill*, licence MIT) gardées en données de chemin SVG et dessinées avec GDI+. |
| `src/settings*.cpp` | Réglages (registre) et fenêtre de paramètres. |
| `legacy/` | Ancienne version 1.x (DeskBand DLL chargée dans Explorer), gardée pour référence. |

L'intégration à la barre des tâches reprend l'approche de [BatteryHub](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor).

## Désinstaller l'ancienne version 1.x (DeskBand)

Si l'ancienne DLL avait été enregistrée, la désactiver dans *clic droit sur la barre des tâches > Barres d'outils*, puis supprimer la clé `HKLM\Software\Classes\CLSID\{C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}` (invite de commandes administrateur) :

```bat
reg delete "HKLM\Software\Classes\CLSID\{C7E3A129-4F8B-4D52-B6A0-3E9F1C245D87}" /f
```

## Licence

[MIT](LICENSE). Les icônes viennent de [Phosphor Icons](https://phosphoricons.com) (MIT) : voir [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
