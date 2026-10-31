# BeatDrop

Minimalistisches VST3-Plugin für FL Studio: YouTube-Beat einfügen oder suchen, anhören, aufnehmen, als fertige Audiodatei in die Playlist ziehen.

## So funktioniert's

- **Link einfügen oder Suchbegriff eintippen** (z. B. `dark trap type beat`), Enter. Bei einer Suche kommen 6 Treffer, anklicken lädt den Beat.
- BeatDrop holt die Audiospur im Hintergrund (yt-dlp + ffmpeg), legt sie als WAV in `Musik\BeatDrop` ab und spielt sie direkt ab.
- **Aufnehmen:** Sobald FL läuft (Play oder Record), spielt der Beat sample-genau synchron zur Playlist-Position. Position 0 = Anfang des Beats. Mikro-Spur im Mixer scharf schalten, Record drücken, rappen.
- **In die Playlist ziehen:** Wellenform oder den Button „Beat in die Playlist ziehen" mit der Maus in die FL-Playlist ziehen. Dort liegt dann die WAV-Datei als normaler Clip.
- Klick auf die Wellenform springt im Vorhör-Modus (FL steht) an die Stelle. Leertaste = Play/Stop im Vorhör-Modus.

## Einrichten (Windows)

**1. Plugin bauen** (eine der beiden Varianten)

*A) Ohne lokale Entwicklungsumgebung:* Ordner als GitHub-Repository hochladen. Unter „Actions" baut `Build BeatDrop (Windows VST3)` automatisch, am Ende den Artifact `BeatDrop-VST3-Windows` herunterladen und entpacken.

*B) Lokal:* Visual Studio 2022 (Workload „Desktopentwicklung mit C++") und CMake installieren, dann in diesem Ordner:

```
cmake -B build -A x64
cmake --build build --config Release --target BeatDrop_VST3
```

Ergebnis: `build\BeatDrop_artefacts\Release\VST3\BeatDrop.vst3`

**2. Download-Werkzeuge installieren** (einmalig): `setup-windows.ps1` per Rechtsklick → „Mit PowerShell ausführen". Das lädt yt-dlp, ffmpeg und Deno (YouTube braucht für yt-dlp inzwischen eine JavaScript-Runtime) nach `%APPDATA%\BeatDrop\bin`.

**3. Plugin installieren:** den Ordner `BeatDrop.vst3` nach `C:\Program Files\Common Files\VST3\` kopieren. In FL: *Options → Manage plugins → Find installed plugins*. Danach im Channel Rack über **+** BeatDrop hinzufügen.

## Wenn etwas nicht klappt

- **Kein Ton in FL:** FL schaltet Plugins stumm, die keine Noten bekommen. Im Plugin-Fenster oben links auf das Zahnrad (Wrapper-Einstellungen) und „Smart disable" ausschalten.
- **Download schlägt fehl:** BeatDrop aktualisiert yt-dlp beim ersten Fehler pro Sitzung automatisch und versucht es erneut. Hilft das nicht, `setup-windows.ps1` nochmal ausführen.
- **Strg+V im Textfeld geht nicht:** Rechtsklick → Einfügen. Oder Feld leer lassen und auf den Pfeil klicken, dann wird der Link aus der Zwischenablage genommen.
- **Lange Videos:** Videos über 15 Minuten und Livestreams werden abgelehnt.

## Technik in Kürze

JUCE 8 (wird beim ersten CMake-Lauf automatisch geladen), C++17. yt-dlp läuft immer in eigenen Threads, nie im Audio-Thread. Die geladene Datei wird mit dem Projekt gespeichert und beim Öffnen wieder geladen, solange sie in `Musik\BeatDrop` liegt.

## Rechtliches

Das Herunterladen von YouTube-Audio verstößt gegen die YouTube-Nutzungsbedingungen, und die Beats gehören ihren Produzenten. Zum Ausprobieren und für Demos ist das üblich (viele „Type Beats" sind dafür freigegeben), für eine Veröffentlichung braucht ihr eine Lizenz (Lease) vom Produzenten.
