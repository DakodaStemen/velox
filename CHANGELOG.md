# Changelog

All notable changes to this project are documented in this file. Versions follow
[Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
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
