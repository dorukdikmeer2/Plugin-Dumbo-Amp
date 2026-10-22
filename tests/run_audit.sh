#!/usr/bin/env bash
# Build and run the whole headless audit of the JUCE-free engine (no JUCE needed).  Usage: ./run_audit.sh
set -e
cd "$(dirname "$0")"
make -j"$(nproc)" check
