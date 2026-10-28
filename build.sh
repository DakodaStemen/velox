#!/usr/bin/env bash
# Builds Scintilla, Lexilla and the application.
#   QT=6 ./build.sh        build against Qt 6 (needs qmake6); default is Qt 5
#   QMAKE=/path/qmake ...  use a specific qmake instead
#   SANITIZE=1 ./build.sh  add AddressSanitizer and UBSan to the application
set -euo pipefail
cd -- "$(dirname -- "$0")"
if [ -n "${QMAKE:-}" ]; then
  qmake_binary="$QMAKE"
elif [ "${QT:-5}" = 6 ]; then
  qmake_binary=qmake6
else
  qmake_binary=$(command -v qmake-qt5 || command -v qmake)
fi
build_jobs="${JOBS:-4}"
# SELFTEST=0 builds without the test suite (packaging); SANITIZE=1 adds ASan+UBSan.
app_config=()
if [ "${SELFTEST:-1}" = 0 ]; then app_config+=(CONFIG-=selftest); else app_config+=(CONFIG+=selftest); fi
qt_major=$("$qmake_binary" -query QT_VERSION | cut -d. -f1)
sanitize="${SANITIZE:-0}"
extra=()
if [ "$sanitize" = 1 ]; then
  app_config+=(CONFIG+=sanitizer)
  extra=("QMAKE_CXXFLAGS+=-fsanitize=address,undefined -fno-omit-frame-pointer"
         "QMAKE_LFLAGS+=-fsanitize=address,undefined")
  export CXXFLAGS="${CXXFLAGS:-} -fsanitize=address,undefined -fno-omit-frame-pointer"
fi
# Scintilla is linked statically from vendor/, so switching Qt major version or
# sanitizers must not reuse objects built the other way.
stamp="qt${qt_major}-san${sanitize}"
if [ "$(cat build/.stamp 2>/dev/null || true)" != "$stamp" ]; then
  rm -rf build/scintilla build/app vendor/scintilla/bin/libScintillaEditBase.a
  (cd vendor/lexilla/src && make clean >/dev/null 2>&1 || true)
fi
mkdir -p build/scintilla build/app
(cd build/scintilla; "$qmake_binary" ../../vendor/scintilla/qt/ScintillaEditBase/ScintillaEditBase.pro CONFIG+=release CONFIG+=staticlib "${extra[@]}"; make -j"$build_jobs")
make -C vendor/lexilla/src -j"$build_jobs"
(cd build/app; "$qmake_binary" ../../velox.pro "${app_config[@]}" ${PREFIX:+PREFIX="$PREFIX"}; make -j"$build_jobs")
echo "$stamp" > build/.stamp
