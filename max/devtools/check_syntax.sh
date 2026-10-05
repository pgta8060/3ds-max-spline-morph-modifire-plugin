#!/usr/bin/env bash
# Compiles every Max layer source against the SDK stand-in (syntax only).
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cxx="${CXX:-clang++}"
status=0
for f in "$root"/max/src/*.cpp; do
  echo "checking $(basename "$f")"
  "$cxx" -std=c++17 -fsyntax-only -Wall -Wextra -Wno-unused-parameter \
    -I"$root/max/devtools/sdk_stub" -I"$root/max/src" -I"$root/core/include" "$f" || status=1
done
exit $status
