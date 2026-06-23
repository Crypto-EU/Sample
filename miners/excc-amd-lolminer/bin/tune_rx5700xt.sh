#!/usr/bin/env bash
set -euo pipefail

MINER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd)"

print_oc_profiles() {
  cat <<'EOF'

RX 5700 XT HiveOS OC start points for EXCC / Equihash 144/5
-----------------------------------------------------------

Start conservative and tune one step at a time. Equihash 144/5 on Navi10
responds strongly to stable core clock and voltage; memory still matters, but
unstable memory clocks usually reduce accepted shares even when displayed speed
looks higher.

Efficiency profile:
  Core clock: 1360-1380 MHz
  Core voltage (VDD): 760-780 mV
  Memory clock: 880-900 MHz
  Memory controller voltage (VDDCI): 800-840 mV
  Memory voltage (MVDD): 1350 mV

Performance profile:
  Core clock: 1410-1430 MHz
  Core voltage (VDD): 790-810 mV
  Memory clock: 900-930 MHz
  Memory controller voltage (VDDCI): 840-880 mV
  Memory voltage (MVDD): 1350-1365 mV

HiveOS tuning loop:
  1. Apply the efficiency profile first.
  2. Run this script and keep the best EXCC_KEEPFREE result.
  3. Increase core by 10 MHz while shares remain accepted and temperatures are safe.
  4. Increase memory by 5-10 MHz only after core is stable.
  5. Compare accepted shares over time, not just the instant displayed hashrate.

Do not flash BIOS or apply MPT-style limit changes unless you already know the
exact card model and have a recovery plan.
EOF
}

if [[ "${1:-}" == "--print-only" ]]; then
  print_oc_profiles
  exit 0
fi

export EXCC_GPU_PROFILE="${EXCC_GPU_PROFILE:-rx5700xt}"
export EXCC_TUNE_SECONDS="${EXCC_TUNE_SECONDS:-120}"
export EXCC_TUNE_KEEPFREE_VALUES="${EXCC_TUNE_KEEPFREE_VALUES:-0 -8 -16 -32 8 16 32}"
export EXCC_HSA_ENABLE_SDMA="${EXCC_HSA_ENABLE_SDMA:-0}"

"${MINER_DIR}/bin/tune_keepfree.sh"
print_oc_profiles
