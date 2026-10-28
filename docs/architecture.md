# Architecture

Velox is a Qt Widgets application around the Scintilla editing component. This page is a map for
contributors.

## Layout

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Startup, command line, application style, and single-window handoff over a local socket |
| `src/window.{h,cpp}` | Main window: tabs, menus, search, find in files, languages, themes, sessions, macros |
| `src/document.{h,cpp}` | File I/O: encoding detection, BOMs, safe saving, external change detection |
| `src/udleditor.{h,cpp}` | The custom language editor: `UdlSpec` (UDL XML read and write) and its dialog |
| `tests/integration.cpp` | Integration tests, compiled into the binary and run with `--self-test` |
| `share/` | Language definitions, themes and bundled user-defined languages, installed to `<prefix>/share/velox` |
| `packaging/` | Desktop entry, icon, AppStream metadata, Arch `PKGBUILD` |
| `vendor/` | Third-party code: Scintilla (Qt backend), Lexilla lexers, Boost.Regex search engine |

## Editing model

Each tab is an `EditorTab` with two `ScintillaEditBase` views sharing one Scintilla document, so the
split view never copies text. All editing goes through Scintilla messages (`SCI_*`). The document
owns its lexer, so setting a language once affects both views.

Text is held internally as UTF-8. UTF-32 is converted by hand in `document.cpp` because Qt 5's codec emits a stray NUL for every surrogate pair. `DocumentData` remembers the file's encoding, BOM, content hash,
size and modification time, and converts back on save.

## Preferences and large files

`Preferences` (in `window.h`) is loaded from `settings.ini` at startup and applied to each tab by
`Window::applyPreferences`; the font and size are applied with the theme in `Window::applyTheme`.
Files over `largeFileSize` (32 MiB) set `EditorTab::large`, which forces the plain-text lexer, turns
folding and wrapping off and skips the outline. Nothing opens above `maxFileSize` (1 GiB), the limit
that Scintilla's int-length text copies allow.

## Saving

`DocumentData::write` refuses to save when:

- the file changed on disk since it was loaded (hash mismatch),
- the target is not a regular file, has multiple hard links, or is read-only,
- the text cannot be represented in the chosen encoding.

Writes go through `QSaveFile`: a temporary file that is atomically renamed over the target.
Symlinks are resolved first so the link itself is preserved.

## Search

Scintilla is built with `SCI_OWNREGEX`, which replaces its built-in regex with the Boost.Regex
engine in `vendor/boostregex`. `Window::searchFlags` chooses the empty-match mode per operation
(find, replace, replace all, mark), which is what makes patterns like `^` behave predictably.
Find in files runs every file through a hidden Scintilla view, so it uses exactly the same matching
rules as in-document search.

## Languages and themes

`share/langs.model.xml` maps languages to Lexilla lexers, file extensions and keyword lists.
Themes in `share/themes` give per-lexer style colors. User-defined languages (UDL 2.x XML, from
`share/userDefineLangs` and `~/.config/velox/userDefineLangs`) drive the generic `user` lexer
through `Window::applyUserLanguage`. `src/udleditor.{h,cpp}` holds `UdlSpec`, which writes the simple
definitions made in Language → Define custom language to `~/.config/velox/userDefineLangs/` (marked
with `veloxEditor="1"` so they can be edited again), and the dialog that edits it.

## Sessions and recovery

Every five seconds the open tabs, including unsaved text, are written to
`~/.local/share/velox/recovery.json` and restored on the next start. Closing normally writes the
same file after the user has chosen to save or discard each modified tab.

## Build

`build.sh` (with `QT=6` for Qt 6, `SANITIZE=1` for ASan/UBSan) builds Scintilla as a static library with qmake, Lexilla with its makefile, and then the
app from `velox.pro`. The result is one self-contained executable. `make install` (in
`build/app`) installs the binary, data, desktop entry, icon and metadata under `PREFIX`.
