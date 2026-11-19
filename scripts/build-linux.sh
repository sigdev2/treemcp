#!/usr/bin/env bash
preset="${1:-linux-debug}"
cmake --preset "$preset" &&
cmake --build --preset "$preset" &&
ctest --preset "$preset"
