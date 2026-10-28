# Contributing to Velox

Thanks for helping. This guide covers how to get a working build, what we expect from changes, and
how to submit them.

## Getting started

Requirements: Qt 5.15 (`qt5-base`, `qt5-svg`), a C++17 compiler and `make`.

```sh
./build.sh
./run.sh
QT_QPA_PLATFORM=offscreen ./run.sh --self-test
```

After the first build, `make -C build/app` rebuilds only the application. [docs/architecture.md](docs/architecture.md)
explains where things live.

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
