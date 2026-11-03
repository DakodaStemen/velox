# One entry point for developers. `make ci` is lint + tests + build, the same as the Linux job in
# .github/workflows/ci.yml minus desktop-metadata validation and packaging.
# Choose the Qt major version with `make ci QT=6` (build.sh defaults to Qt 5).
.DEFAULT_GOAL := help
.PHONY: help setup lint test build ci clean
SH_FILES := build.sh run.sh scripts/bench.sh scripts/check-version.sh packaging/appimage/build-appimage.sh
JOBS ?= 4
export JOBS

help:
	@echo "make setup   check that the build tools are installed"
	@echo "make lint    version consistency, Python syntax, shellcheck (errors only), markdownlint"
	@echo "make build   ./build.sh (QT=6 for Qt 6, SANITIZE=1 for ASan + UBSan)"
	@echo "make test    script tests, then the in-binary self-test (needs a prior make build)"
	@echo "make ci      lint + build + test"
	@echo "make clean   remove build output"

setup:
	@command -v g++ >/dev/null && command -v make >/dev/null || { echo "need g++ and make" >&2; exit 1; }
	@if [ "$(QT)" = 6 ]; then command -v qmake6 >/dev/null || { echo "need qmake6 (Qt 6)" >&2; exit 1; }; \
	else command -v qmake-qt5 >/dev/null || command -v qmake >/dev/null || { echo "need qmake (Qt 5)" >&2; exit 1; }; fi
	@command -v shellcheck >/dev/null || echo "note: shellcheck not found; make lint will fail"
	@command -v npx >/dev/null || echo "note: npx not found; make lint skips markdownlint"

lint:
	scripts/check-version.sh
	PYTHONDONTWRITEBYTECODE=1 python3 -I -m py_compile scripts/check-hardening.py
	shellcheck --severity=error $(SH_FILES)
	@if command -v npx >/dev/null; then npx -y markdownlint-cli2 "**/*.md"; else echo "skip markdownlint: npx not found"; fi

build:
	./build.sh

test:
	tests/scripts_test.sh
	QT_QPA_PLATFORM=offscreen ./run.sh --self-test

ci: lint build test

clean:
	rm -rf build vendor/lexilla/bin/*.a vendor/scintilla/bin/libScintillaEditBase.a
