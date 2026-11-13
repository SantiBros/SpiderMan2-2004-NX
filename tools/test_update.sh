#!/usr/bin/env bash
# test_update.sh <default.xbe>: the in-app update's own parts (switch/update_core.cpp) tested on a computer. Builds
# tools/test_update.cpp with OpenSSL, locks a made-up .nro with tools/lock_nro.py, serves it with
# tools/test_update_server.py, and reads GitHub's real list of releases (with the gh tool, else a saved one).
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
XBE=${1:?usage: test_update.sh <default.xbe>}
T=$(mktemp -d)
trap 'kill $SERVER 2>/dev/null || true; rm -rf "$T"' EXIT
g++ -std=c++20 -O2 -Wall -I"$R" -I"$R/sdk/runtime" "$R/tools/test_update.cpp" "$R/switch/update_core.cpp" -lcrypto -o "$T/test_update"
head -c 3000017 /dev/urandom > "$T/made.nro"  # an odd size: the last block is a part
python3 "$R/tools/lock_nro.py" lock "$T/made.nro" "$XBE" "$T/made.nro.locked" > /dev/null
if gh api "repos/SantiBros/SpiderMan2-2004-NX/releases?per_page=20" > "$T/releases.json" 2> /dev/null; then
    echo "GitHub's list of releases read"
else
    echo '[{"assets":[{"name":"SpiderMan2Xbox-0.3.1.nro.locked","size":1,"browser_download_url":"https://github.com/SantiBros/SpiderMan2-2004-NX/releases/download/v0.3.1/SpiderMan2Xbox-0.3.1.nro.locked"}]}]' > "$T/releases.json"
    echo "GitHub not reachable: a made-up list of releases"
fi
PORT=$((20000 + RANDOM % 20000))
python3 "$R/tools/test_update_server.py" "$T/made.nro.locked" "$PORT" &
SERVER=$!
for _ in $(seq 50); do (exec 3<>/dev/tcp/127.0.0.1/$PORT) 2> /dev/null && break; sleep 0.1; done
"$T/test_update" "$T/releases.json" "$T/made.nro.locked" "$XBE" "$T/made.nro" "$PORT"
