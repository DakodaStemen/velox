#!/usr/bin/env bash
# Tests for the release helper scripts (scripts/check-version.sh, scripts/check-hardening.py).
# Needs no Qt and no build. Exit 0 when all checks pass, 1 when any fails.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/velox-scripts-test.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
pass=0
fail=0

check() { # check NAME EXPECTED_EXIT CMD...
  local name=$1 want=$2 got=0
  shift 2
  "$@" >"$work/out.txt" 2>&1 || got=$?
  if [ "$got" -eq "$want" ]; then
    pass=$((pass + 1)); echo "ok   $name"
  else
    fail=$((fail + 1)); echo "FAIL $name (exit $got, wanted $want)"; sed 's/^/     /' "$work/out.txt"
  fi
}

# Copy only what check-version.sh reads, so the mutation tests never touch the real tree.
copy_tree() {
  local dest=$1
  mkdir -p "$dest/scripts" "$dest/packaging/aur"
  cp "$root/velox.pro" "$root/CHANGELOG.md" "$dest/"
  cp "$root/scripts/check-version.sh" "$dest/scripts/"
  cp "$root/packaging/PKGBUILD" "$dest/packaging/"
  cp "$root/packaging/aur/PKGBUILD" "$dest/packaging/aur/"
  cp "$root/packaging/io.github.DakodaStemen.Velox.metainfo.xml" "$dest/packaging/"
}

copy_tree "$work/ok"
check "check-version passes on the repo versions" 0 bash "$work/ok/scripts/check-version.sh"

copy_tree "$work/bad-pro"
sed -i 's/^VERSION *=.*/VERSION = 9.9.9/' "$work/bad-pro/velox.pro"
check "check-version fails when velox.pro differs" 1 bash "$work/bad-pro/scripts/check-version.sh"

copy_tree "$work/bad-meta"
sed -i 's/<release version="[^"]*"/<release version="9.9.9"/' "$work/bad-meta/packaging/io.github.DakodaStemen.Velox.metainfo.xml"
check "check-version fails when AppStream differs" 1 bash "$work/bad-meta/scripts/check-version.sh"

copy_tree "$work/no-version"
sed -i '/^VERSION *=/d' "$work/no-version/velox.pro"
check "check-version fails when velox.pro has no VERSION" 1 bash "$work/no-version/scripts/check-version.sh"

check "check-hardening fails on a missing binary" 1 python3 -I "$root/scripts/check-hardening.py" "$work/does-not-exist"
check "check-hardening --help exits 0" 0 python3 -I "$root/scripts/check-hardening.py" --help

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
