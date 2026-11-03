# Contributing to Velox

Thanks for helping. This guide covers how to get a working build, what we expect from changes, and
how to submit them.

## Getting started

Requirements: Qt 5.15 (`qt5-base`, `qt5-svg`) or Qt 6 (`qt6-base`, `qt6-svg`; build with `QT=6`), a C++17
compiler and `make`.

```sh
./build.sh
./run.sh
QT_QPA_PLATFORM=offscreen ./run.sh --self-test
```

`build.sh` includes the test suite (needs `qt5-base`'s Qt Test module). `SELFTEST=0 ./build.sh`
produces the production binary without it, and `SANITIZE=1 ./build.sh` adds ASan + UBSan. Run
`scripts/check-version.sh` when touching version numbers.

After the first build, `make -C build/app` rebuilds only the application. [docs/architecture.md](docs/architecture.md)
explains where things live.

## Checks before you push

`make ci` runs lint (version consistency, `shellcheck --severity=error`, Python syntax check,
markdownlint), `./build.sh`, the script tests in `tests/scripts_test.sh` and `velox --self-test`.
Pass `QT=6` for a Qt 6 build. Optional: `pre-commit install` runs fast checks on each commit
(`.pre-commit-config.yaml`).

## Branches and pull requests

- Branch prefixes: `feat/`, `fix/`, `docs/`, `chore/`, `ci/`, `test/`.
- PR titles follow Conventional Commits (`feat: ...`, `fix: ...`, `chore: ...`), one purpose per PR.
- Decisions that shape the architecture are recorded in [docs/adr](docs/adr/README.md).

## Reporting bugs and requesting features

Search existing issues first. For bugs, include your distribution, Velox version
(`velox --version`), steps to reproduce, and what you expected. Attach a small sample file when the
problem depends on file contents or encoding.

## Making changes

- Keep pull requests focused on one change. Open an issue first for large features so the design
  can be agreed before you invest time.
- Match the surrounding code: 2-space indentation, LLVM-based formatting (`.clang-format`), and
  comments that explain *why* rather than *what*.
- Add or update tests in `tests/integration.cpp` for behavior changes. File handling changes need
  byte-level round-trip tests.
- Never introduce a code path that can lose user data. Saves must stay atomic, and refusing to save is
  better than writing something the user did not intend.
- Update `CHANGELOG.md` under *Unreleased*.
- `vendor/` holds third-party code. Avoid changing it. When you must, keep the original headers,
  mark the change, and record it in `THIRD_PARTY_NOTICES.md`.

## Commit messages

Use the imperative mood ("Add find in files", not "Added…"), keep the subject under about 70
characters, and explain the reason for the change in the body when it is not obvious.

## Licensing

By submitting a contribution you agree that it is licensed under the GNU General Public License,
version 3 or later, the same as the rest of the project. Only submit work you have the right to
license this way.
