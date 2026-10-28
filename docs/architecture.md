# Architecture

Velox is a Qt Widgets application around the Scintilla editing component. This page is a map for
contributors.

## Layout

| Path | Contents |
| --- | --- |
| `src/main.cpp` | Startup, command line, application style, and single-window handoff over a local socket |
| `src/window.{h,cpp}` | Main window: tabs, menus, search, find in files, languages, themes, sessions, macros |
| `src/document.{h,cpp}` | File I/O: encoding detection, BOMs, safe saving, external change detection |
| `tests/integration.cpp` | Integration tests, compiled in only when `CONFIG+=selftest` is set (`build.sh` default; `SELFTEST=0` disables), run with `--self-test` |
| `share/` | Language definitions, themes and bundled user-defined languages, installed to `<prefix>/share/velox` |
| `packaging/` | Desktop entry, icon, AppStream metadata, Arch `PKGBUILD` (in-tree) and `aur/PKGBUILD` (release tarball) |
| `vendor/` | Third-party code: Scintilla (Qt backend), Lexilla lexers, Boost.Regex search engine |

## Editing model

Each tab is an `EditorTab` with two `ScintillaEditBase` views sharing one Scintilla document, so the
split view never copies text. All editing goes through Scintilla messages (`SCI_*`). The document
owns its lexer, so setting a language once affects both views.

Text is held internally as UTF-8. `DocumentData` remembers the file's encoding, BOM, content hash,
size and modification time, and converts back on save.

## Saving

`DocumentData::write` refuses to save when:

- the file changed on disk since it was loaded (hash mismatch),
- the target is not a regular file, has multiple hard links, or is read-only,
- the text cannot be represented in the chosen encoding.

Writes go through `QSaveFile`: a temporary file that is atomically renamed over the target.
Symlinks are resolved first so the link itself is preserved, and permission bits are kept.
Because the temporary file is renamed over the target, ownership, ACLs and extended attributes are not preserved.

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
through `Window::applyUserLanguage`.

## Sessions and recovery

Every five seconds the open tabs, including unsaved text, are written to
`~/.local/share/velox/recovery.json` and restored on the next start. Closing normally writes the
same file after the user has chosen to save or discard each modified tab.

## Build

`build.sh` builds Scintilla as a static library with qmake, Lexilla with its makefile, and then the
app from `velox.pro`. The result is one self-contained executable. `make install` (in
`build/app`) installs the binary, data, desktop entry, icon and metadata under `PREFIX`.
