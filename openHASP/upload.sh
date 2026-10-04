#!/bin/bash
# Upload files to the openHASP LittleFS via the web editor endpoint (/edit).
# Usage: ./upload.sh <ip> [file ...]
#  default: pages.jsonl and all *.cmd from data-files/ (not mqtt-demo.jsonl)
# Set HASP_USER/HASP_PASS if a web password is configured.

set -e

IP="${1:-192.168.200.123}"
shift

FILES=${@:-$(ls "$(dirname "$0")"/data-files/*.cmd "$(dirname "$0")"/data-files/pages.jsonl)}
AUTH=${HASP_PASS:+-u ${HASP_USER:-admin}:$HASP_PASS}

for f in $FILES; do
    echo "uploading $(basename "$f") -> /$(basename "$f")"
    curl -s -f $AUTH -F "data=@$f;filename=/$(basename "$f")" "http://$IP/edit" >/dev/null
done

echo; echo "files on device:"
curl -s $AUTH "http://$IP/list?dir=/" | jq
