#!/usr/bin/env bash
# End-to-end smoke test: starts the real server and drives it over HTTP.
# Usage: scripts/e2e.sh        (build first with `make`)
cd "$(dirname "$0")/.." || exit 1

PORT=18080
BASE="http://127.0.0.1:$PORT"
BIN=./build/hospital_scheduler
FAILS=0
CHECKS=0

[ -x "$BIN" ] || { echo "Build first: make"; exit 1; }

$BIN --port $PORT --tick-seconds 0 >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null; wait $SERVER 2>/dev/null' EXIT

for _ in $(seq 1 50); do
    curl -s -o /dev/null "$BASE/api/state" && break
    sleep 0.1
done

check() {   # check "description" "actual" "expected-substring"
    CHECKS=$((CHECKS + 1))
    if [[ "$2" == *"$3"* ]]; then
        echo "  ok   $1"
    else
        echo "  FAIL $1"; echo "       expected to contain: $3"; echo "       got: ${2:0:200}"
        FAILS=$((FAILS + 1))
    fi
}

get()  { curl -s --max-time 5 "$BASE$1"; }
post() { curl -s --max-time 5 -X POST -d "${2:-}" "$BASE$1"; }
code() { curl -s --max-time 5 -o /dev/null -w '%{http_code}' "$@"; }

echo "Static assets"
check "index.html"  "$(get /)" "<title>Hospital Bed Scheduler</title>"
check "styles.css"  "$(get /styles.css)" "--primary"
check "app.js"      "$(get /app.js)" "Hospital Bed Scheduler - browser UI"
check "no CDN use"  "$(get / | grep -c 'https\?://')" "0"

echo "Acceptance scenario"
check "start at 09:00"       "$(get /api/state)" '"time_str":"09:00"'
check "load demo"            "$(post /api/demo/load)" '"success":true'
check "3 beds occupied"      "$(get /api/statistics)" '"occupied_beds":3'
check "9 patients waiting"   "$(get /api/statistics)" '"waiting":9'
check "5 future arrivals"    "$(get /api/statistics)" '"scheduled":5'
check "advance 90 minutes"   "$(post /api/simulation/advance '{"minutes":90}')" '"time_str":"10:30"'
check "two completions"      "$(get /api/statistics)" '"completed":2'
check "turnaround 90 min"    "$(get /api/patients)" '"turnaround_time":90'
check "start"                "$(post /api/simulation/start)" '"status":"running"'
check "pause"                "$(post /api/simulation/pause)" '"status":"paused"'
get /api/state >/dev/null; get /api/state >/dev/null
check "clock unchanged by polling" "$(get /api/state)" '"time_str":"10:30"'
check "reset"                "$(post /api/simulation/reset)" '"time_str":"09:00"'
check "beds free after reset" "$(get /api/statistics)" '"available_beds":3'

echo "Error handling"
check "malformed JSON -> 400"   "$(code -X POST -d '{bad' $BASE/api/patients)" "400"
check "unknown route -> 404"    "$(code $BASE/api/nope)" "404"
check "wrong method -> 405"     "$(code -X DELETE $BASE/api/state)" "405"
check "oversized body -> 413"   "$(code -X POST -H 'Content-Length: 999999' -d x $BASE/api/patients)" "413"

echo "Request boundaries (headers and body arrive in pieces)"
exec 3<>/dev/tcp/127.0.0.1/$PORT
printf 'POST /api/simulation/advance HTTP/1.1\r\nHost: x\r\nContent-Len' >&3; sleep 0.2
printf 'gth: 13\r\n\r\n{"minutes"' >&3; sleep 0.2
printf ':5}' >&3
REPLY=$(timeout 3 cat <&3)
exec 3>&-
check "fragmented POST" "$REPLY" '"time_str":"09:05"'

echo
echo "$CHECKS checks, $FAILS failed"
[ "$FAILS" -eq 0 ]
