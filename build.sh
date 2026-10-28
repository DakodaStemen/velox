#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p build/scintilla build/app
qmake_binary="${QMAKE:-qmake}"
build_jobs="${JOBS:-4}"
(cd build/scintilla; "$qmake_binary" ../../vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.pro CONFIG+=release CONFIG+=staticlib; make -j"$build_jobs")
make -C vendor/lexilla/src -j"$build_jobs"
(cd build/app; "$qmake_binary" ../../velox.pro ${PREFIX:+PREFIX="$PREFIX"}; make -j"$build_jobs")
