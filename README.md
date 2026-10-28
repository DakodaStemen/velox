<p align="center">
  <img src="packaging/io.github.DakodaStemen.Velox.svg" width="96" alt="">
</p>

<h1 align="center">Velox</h1>

<p align="center">A fast, lightweight native text and code editor for Linux.</p>

<p align="center">
  <a href="https://github.com/DakodaStemen/velox/actions/workflows/ci.yml"><img src="https://github.com/DakodaStemen/velox/actions/workflows/ci.yml/badge.svg" alt="CI status"></a>
</p>

<p align="center"><img src="docs/demo.gif" alt="Animated tour of Velox: highlighting, themes, multiple cursors, regex search, find in files, split view, line tools, encodings, macros, preferences and custom languages" width="820"></p>

<p align="center"><sub>A tour of the features, rendered from the real editor by <a href="tests/demo.cpp"><code>tests/demo.cpp</code></a>; regenerate it with <code>docs/make-demo.sh</code>.</sub></p>

## Features

- **Tabs and split views.** Two views of the same document, drag and drop, recent files, a folder sidebar and a document outline.
- **Syntax highlighting** for about 95 languages, 22 color themes, keyword completion and code folding.
- **User-defined languages.** Define one in Language → Define custom language (keywords, comments, strings, operators and colors), or describe it in a UDL 2.x XML file. Markdown definitions are included.
- **Search.** Regular expression find and replace with captures and zero-width matches (`^`, `$`, lookarounds), *Mark all*, and *Find in files* with a clickable results panel.
- **Careful file handling.** UTF-8 (with or without BOM), UTF-16 and UTF-32 (LE/BE) and legacy code pages. Saves are atomic, refuse to lose characters or overwrite files changed by other programs, and preserve symlinks, hard links and permissions.
- **Editing power.** Multiple cursors, rectangular selection (Alt+drag), line ending conversion, bookmarks, macro recording and playback, comment toggling, line sorting, de-duplication, joining and moving.
- **Large files.** Files up to 1 GiB open; above 32 MiB highlighting, folding and the outline are switched off to stay responsive.
- **Preferences.** Font, tab width, spaces or tabs, line numbers, wrapping, and optional trailing-whitespace trimming and final newline on save (Edit → Preferences).
- **Never lose work.** Unsaved text is snapshotted every few seconds and restored after a crash. Changes made to open files by other programs are detected.
- **One window.** Running `velox file.txt` while Velox is open adds a tab to the existing window.

## Installation

### Arch Linux

```sh
git clone https://github.com/DakodaStemen/velox.git
cd velox/packaging
makepkg -si
```

The package installs a single self-contained binary, desktop entry, icon and AppStream metadata.
To make Velox the default editor for plain text:

```sh
xdg-mime default io.github.DakodaStemen.Velox.desktop text/plain
```

### Other distributions

Install Qt 5.15 or Qt 6 (Widgets, XML, Network, Test and the SVG image plugin), a C++17 compiler and `make`, then:

```sh
./build.sh                                 # Qt 5; use QT=6 ./build.sh for Qt 6
sudo make -C build/app install          # installs to /usr; set PREFIX=... when running build.sh to change it
```

## Usage

```
velox [options] [files…]

  FILE:LINE[:COLUMN]  open a file with the cursor at a position
  --new-instance   open a separate window instead of reusing a running one
  --fresh          start without restoring the previous session
  --self-test      run the integration test suite in an isolated profile
  --version, --help
```

| Shortcut | Action |
| --- | --- |
| Ctrl+F / Ctrl+H | Find / Replace |
| Ctrl+Shift+F | Find in files |
| F3 / Shift+F3 | Next / previous match |
| Ctrl+G | Go to line |
| Ctrl+F2, F2 | Toggle bookmark, next bookmark |
| Ctrl+D | Duplicate line or selection |
| Ctrl+Alt+S | Split view |
| Ctrl+Shift+R / Ctrl+Shift+P | Record / play macro |
| Alt+Z | Toggle word wrap |
| Ctrl+/ | Toggle comment |
| Alt+Up / Alt+Down | Move line up / down |
| Ctrl+J | Join lines |
| Ctrl+, | Preferences |

## Configuration

| What | Location |
| --- | --- |
| Settings | `~/.config/velox/settings.ini` |
| Your user-defined languages | `~/.config/velox/userDefineLangs/*.xml` |
| Crash recovery snapshot | `~/.local/share/velox/recovery.json` |
| Bundled languages and themes | `/usr/share/velox` (override with `VELOX_DATA_DIR`) |

**Help → Open configuration folder** creates and opens the configuration directory.

## Building from source

```sh
./build.sh                                 # Scintilla, Lexilla and the application (QT=6 for Qt 6)
SANITIZE=1 ./build.sh                      # with AddressSanitizer and UBSan, for testing
./run.sh [files…]                          # run from the build tree
QT_QPA_PLATFORM=offscreen ./run.sh --self-test
```

See [docs/architecture.md](docs/architecture.md) for a tour of the code.

## Roadmap

- Qt 6 as the default (it builds with `QT=6 ./build.sh` and has a CI job; Qt 5 remains supported)
- Flatpak package (a manifest is in `packaging/`, not yet tested)
- Plugin API

## Contributing

Bug reports, ideas and pull requests are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) and
the [Code of Conduct](CODE_OF_CONDUCT.md). Security issues: see [SECURITY.md](SECURITY.md).

## License

Velox is free software, released under the [GNU General Public License v3.0 or later](LICENSE).
It bundles third-party components under their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
