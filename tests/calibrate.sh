#!/usr/bin/env bash
# Re-measure the loudness calibration and rewrite Source/Amps/Calibration.h (see docs/GAIN_STAGING.md).
set -e
cd "$(dirname "$0")"
make -j"$(nproc)" calibrate
