# Changelog

All notable changes to this project are documented in this file. Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- README animation (`docs/demo.gif`), rendered from the real window by an opt-in demo script (`CONFIG+=demo` / `DEMO=1 ./build.sh`, `velox --demo DIR`) and the manual `Demo` workflow. Release and default builds do not contain it.

### Changed
- New minimal logo: a single blue V on a dark tile.
- The test suite and `--self-test` are now a build option (`CONFIG+=selftest`); `build.sh` enables it by default and `SELFTEST=0` (used by the PKGBUILDs) produces a production binary without Qt Test.
- The version is defined once in `velox.pro`; `scripts/check-version.sh` checks it against the PKGBUILD, AppStream metadata and CHANGELOG.
- PKGBUILD builds out of tree; added `packaging/aur/PKGBUILD` for release-tarball builds.

### Added
- CI: Ubuntu 24.04 build, ASan + UBSan build (`CONFIG+=sanitizer`), version consistency check, least-privilege permissions, pinned actions and Dependabot updates.
- Tag-triggered release workflow that publishes a source tarball with notes from this changelog.
### Fixed
- 16 languages (ActionScript, ASP, AutoIt, Baan C, COBOL, Fortran 77, Go, JavaScript (js), JSON5, JSP, NFO, PHP, PostScript, RC, Scheme, Swift) had no matching Lexilla lexer and rendered as plain text; they now map to the correct lexers (NFO stays plain text). JSON5 allows comments. A self-test guards against future regressions.
### Added
- Files that are not valid UTF-8 can be reopened with a chosen legacy encoding; the encoding lists now also include ISO-8859-15, EUC-KR, Big5 and Windows-1251.

### Fixed
- Auto-indent now also works for CR-only documents.
- Completion is triggered by non-ASCII letters.
- Find in files no longer cuts a UTF-8 character in half when shortening long result lines.

### Documentation
- Documented what atomic saves do and do not preserve (ownership, ACLs and extended attributes).
### Fixed
- Crash recovery: each `--new-instance` window now snapshots into its own `recovery-<pid>.json` instead of overwriting the shared `recovery.json`, and no longer restores the primary window's session. The primary instance adopts recovery files left by dead windows (deleted only after their tabs are re-snapshotted); a clean exit removes the window's own file. The legacy `recovery.json` format still restores.
- Macros: opening, reloading, restoring or creating documents while recording no longer records the file contents; text for `SCI_APPENDTEXT`, `SCI_SEARCHNEXT` and `SCI_SEARCHPREV` is copied so playback never reads freed memory.
- Single-instance socket and lock file now live in `$XDG_RUNTIME_DIR` when available (falling back to `/tmp`); forwarded data is capped at 1 MiB, must be absolute paths, and a client that stalls is dropped after 5 s.
- Session restore reports missing files in one summary instead of one dialog per file, and rejects recovery backups that declare an absurd uncompressed size.
- The document outline is only built while its dock is visible and scans lines through Scintilla with size caps instead of copying the whole text.
- Recovery snapshots only read and compress text for modified or untitled tabs, and skip writing `recovery.json` when nothing changed (file format unchanged).
- Opening large files uses less peak memory (chunked decoding, no extra copy for UTF-8).

### Added
- Find in files has a Stop button (and Escape), cannot be started twice, disables its button while running, skips files that take too long, and no longer hashes files it only searches.

### Added
- Preferences dialog (Edit → Preferences) for font, tab width, spaces or tabs, line numbers,
  current-line highlight, wrapping, whitespace display, auto-indent, completion, and trimming
  trailing whitespace or adding a final newline on save.
- UTF-32 (LE/BE) files can be opened and saved.
- Files up to 1 GiB open. Above 32 MiB, highlighting, folding, wrapping and the outline are off.
- Find in files can read legacy-encoded files by choosing an encoding, and reports how many files
  it skipped. More legacy encodings are offered for "Open with encoding".
- Edit commands: toggle comment (Ctrl+/) for 74 languages, sort lines, remove duplicate lines,
  join lines, and move line up or down.
- Language → Define custom language: a dialog for keywords, comments, strings, operators and colors
  that writes a UDL file to the configuration folder; definitions made there can be edited or deleted.
- `velox FILE:LINE[:COLUMN]` opens a file at a position.
- Qt 6 build support (`QT=6 ./build.sh`), a sanitizer build (`SANITIZE=1 ./build.sh`), CI jobs for
  both, and a Flatpak manifest.

### Changed
- The crash-recovery snapshot no longer copies the text of unmodified saved files.

## [0.1.0] - 2026-10-28

First public release.

### Added
- Tabbed editing with split views, folder sidebar, document outline and recent files.
- Syntax highlighting for about 95 languages, 22 themes, keyword completion and folding.
- User-defined languages from UDL 2.x XML files, with bundled Markdown definitions.
- Regular expression find and replace, Mark all, and Find in files with a results panel.
- Encoding-aware, atomic saves that protect symlinks, hard links, permissions and external edits.
- Crash recovery, external change detection for all open tabs, bookmarks and macros.
- Single-window mode: launching with files while Velox is running opens them as tabs.
- Arch Linux package, desktop entry, icon and AppStream metadata.
- Integration and edge-case test suites, run with `velox --self-test`.
