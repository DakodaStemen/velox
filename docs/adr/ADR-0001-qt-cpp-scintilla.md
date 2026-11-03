# ADR-0001: C++17 on Qt with a vendored Scintilla editing component

| Field | Value |
| :---- | :---- |
| Status | Accepted |
| Date | 2026-11-03 |
| Deciders | @DakodaStemen |

## Context

Velox is a native text and code editor for Linux (also built on Windows and macOS in CI). It needs
fast startup, a text component that handles large files, syntax highlighting for many languages,
and one self-contained binary. The code shows the choices already made: C++17 (`CMakeLists.txt`),
Qt 5.15 or Qt 6 for the UI (`velox.pro`, `CMakeLists.txt`), and Scintilla and Lexilla copied into
`vendor/` as the editing and lexing components.

## Decision

We will write Velox in C++17 on Qt Widgets, supporting Qt 5 and Qt 6. Scintilla (Qt port) and
Lexilla are vendored under `vendor/` and linked statically. `build.sh` with qmake (`velox.pro`) is
the primary local build, and CMake (`CMakeLists.txt`) is used for cross-platform CI builds and
packaging. The in-binary test suite (`tests/`, run as `velox --self-test`) uses Qt Test and is a
build option (`CONFIG+=selftest`, `VELOX_SELFTEST`).

## Alternatives considered

| Alternative | Why not chosen |
| :---- | :---- |
| A web-based shell | Not used; the code base is native C++ and ships as one binary. |
| Another toolkit (for example GTK) | Not used; the code and packaging are written against Qt 5 and 6. |
| Writing a new text widget | Not used; the vendored Scintilla and Lexilla already provide the editing and lexing code. |

## Consequences

**Positive**

- One native binary per platform; Scintilla and Lexilla behavior is fixed by the vendored copy.
- Qt 5 and Qt 6 builds are both exercised in CI.

**Negative**

- Two build systems (qmake and CMake) must stay in sync; `scripts/check-version.sh` guards the version only.
- `vendor/` changes must be recorded in `THIRD_PARTY_NOTICES.md` and are excluded from formatting and lint.
- The test suite needs the Qt Test module and a `QT_QPA_PLATFORM=offscreen` display for headless runs.

**Follow-ups**

- None recorded.
