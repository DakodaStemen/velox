#!/usr/bin/env bash
# Fails if the version differs between velox.pro, the PKGBUILDs, AppStream metadata and CHANGELOG.
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
pro=$(sed -n 's/^VERSION *= *//p' velox.pro | head -n1 | tr -d ' \r')
pkg=$(sed -n 's/^pkgver=\([^ #]*\).*/\1/p' packaging/PKGBUILD | head -n1)
aur=$(sed -n 's/^pkgver=\([^ #]*\).*/\1/p' packaging/aur/PKGBUILD | head -n1)
meta=$(sed -n 's/.*<release version="\([^"]*\)".*/\1/p' packaging/io.github.DakodaStemen.Velox.metainfo.xml | head -n1)
log=$(sed -n 's/^## \[\([0-9][^]]*\)\].*/\1/p' CHANGELOG.md | head -n1)
printf 'velox.pro=%s PKGBUILD=%s aur/PKGBUILD=%s metainfo=%s CHANGELOG=%s\n' "$pro" "$pkg" "$aur" "$meta" "$log"
[ -n "$pro" ] || { echo "no VERSION in velox.pro" >&2; exit 1; }
for v in "$pkg" "$aur" "$meta" "$log"; do
  [ "$v" = "$pro" ] || { echo "version mismatch" >&2; exit 1; }
done
echo "versions consistent: $pro"
