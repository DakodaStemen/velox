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
