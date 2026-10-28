# Changelog

All notable changes to this project are documented in this file. Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

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
- An animated feature tour in the README, generated from the real editor (`docs/make-demo.sh`).
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
