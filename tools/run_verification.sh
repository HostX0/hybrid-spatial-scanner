#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${HSS_VERIFICATION_OUT:-$ROOT/verification_runs/$(date -u +%Y%m%dT%H%M%SZ)}"
mkdir -p "$OUT"
cd "$ROOT"
python -m pytest -q --junitxml="$OUT/pytest.xml" 2>&1 | tee "$OUT/pytest.log"
cmake -S . -B "$OUT/build" -DHSS_ENABLE_SANITIZERS=ON 2>&1 | tee "$OUT/cmake.log"
cmake --build "$OUT/build" 2>&1 | tee -a "$OUT/cmake.log"
ctest --test-dir "$OUT/build" --output-on-failure 2>&1 | tee "$OUT/ctest.log"
if command -v swiftc >/dev/null 2>&1; then
  swiftc -typecheck -I include reference/swift/ContractSmoke.swift 2>&1 | tee "$OUT/swift.log"
  printf 'Swift C import type-check passed. This is not an iOS build.\n' | tee -a "$OUT/swift.log"
else
  printf 'NOT RUN: swiftc is unavailable.\n' | tee "$OUT/swift.log"
fi
printf 'Host checks finished. Physical iPhone, Metal and production-engine tests are NOT certified by this script.\n'
