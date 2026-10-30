#!/usr/bin/env bash
# Builds if needed, then serves the dashboard on http://127.0.0.1:8080
# Extra arguments are passed on: scripts/run.sh --port 9000
cd "$(dirname "$0")/.." && make && exec ./build/hospital_scheduler "$@"
