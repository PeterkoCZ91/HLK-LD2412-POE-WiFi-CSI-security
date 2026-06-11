#!/usr/bin/env bash
# T2 (IMPROVEMENTS): cppcheck statická analýza — stejná invokace lokálně i v CI.
# False positives potlačuj inline: // cppcheck-suppress <id> ; důvod
set -euo pipefail
cd "$(dirname "$0")/.."

cppcheck \
  --enable=warning,performance,portability \
  --std=c++17 --language=c++ \
  --inline-suppr \
  --suppress=missingInclude --suppress=missingIncludeSystem \
  --suppress=unknownMacro \
  -I include \
  -D USE_CSI=1 -D USE_ETHERNET=1 -D NO_BLUETOOTH=1 \
  -D RADAR_RX_PIN=33 -D RADAR_TX_PIN=32 -D RADAR_OUT_PIN=4 \
  -D 'FW_VERSION="ci"' \
  --error-exitcode=2 \
  --quiet \
  src include/services
