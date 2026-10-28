#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/scintilla build/app
qmake_binary="${QMAKE:-qmake}"
build_jobs="${JOBS:-4}"
# SELFTEST=0 builds without the test suite (packaging); SANITIZE=1 adds ASan+UBSan.
app_config=()
if [ "${SELFTEST:-1}" = 0 ]; then app_config+=(CONFIG-=selftest); else app_config+=(CONFIG+=selftest); fi
if [ "${SANITIZE:-0}" = 1 ]; then app_config+=(CONFIG+=sanitizer); fi
(cd build/scintilla; "$qmake_binary" ../../vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.pro CONFIG+=release CONFIG+=staticlib; make -j"$build_jobs")
make -C vendor/lexilla/src -j"$build_jobs"
(cd build/app; "$qmake_binary" ../../velox.pro "${app_config[@]}" ${PREFIX:+PREFIX="$PREFIX"}; make -j"$build_jobs")
