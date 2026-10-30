#!/usr/bin/env bash
# Runs the C unit/API tests and then the HTTP end-to-end script.
cd "$(dirname "$0")/.." || exit 1
make && make test && ./scripts/e2e.sh
