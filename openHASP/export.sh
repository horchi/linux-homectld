#!/bin/bash
# Export our firmware changes from the working tree src/ into the repository:
#   overlay/  - files we added (copied verbatim over the upstream tree)
#   patches/openhasp.patch - our changes to upstream files
# Run this after editing in src/, then commit overlay/ and patches/ (src/ itself is not versioned).
# Usage: ./export.sh

set -e
cd "$(dirname "$0")"
. ./upstream.conf

[ -d src/.git ] || { echo "src/ missing, nothing to export"; exit 1; }

# files we added (not in upstream): listed here, keep in sync with build.sh's overlay copy
OVERLAY_FILES="platformio_override.ini
user_setups/custom/crowpanel-advance.ini
src/custom/my_custom.cpp
src/custom/my_custom.h"

rm -rf overlay && mkdir -p overlay patches

for f in $OVERLAY_FILES; do
    mkdir -p "overlay/$(dirname "$f")"
    cp "src/$f" "overlay/$f"
done

# changes to upstream files: everything tracked upstream except the overlay files and .gitignore
EXCLUDE=""
for f in $OVERLAY_FILES; do EXCLUDE="$EXCLUDE ':!$f'"; done
eval git -C src diff "$OPENHASP_COMMIT" -- . "$EXCLUDE" "':!.gitignore'" > patches/openhasp.patch

echo "overlay:"; find overlay -type f | sed 's/^/  /'
echo "patch:"; git -C src diff --stat "$OPENHASP_COMMIT" -- . $(for f in $OVERLAY_FILES; do echo ":!$f"; done) ':!.gitignore' | sed 's/^/  /'
