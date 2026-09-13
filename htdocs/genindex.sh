#!/bin/bash
# -----------------------------------------------------------------------------
# genindex.sh - generate index.html from index.html.in
#
#  Every local .css / .js reference gets '?v=<hash>' appended, the hash is taken
#  from the content of the referenced file. So a changed file gets a new URL with
#  the next 'make' (also during development, no version bump needed), unchanged
#  files (e.g. the libraries) keep theirs and stay cached in the browsers.
#  index.html itself is delivered with 'no-store'.
# -----------------------------------------------------------------------------

IN="${1:-index.html.in}"
OUT="${2:-index.html}"
missing=0

hash_of()
{
   if [ -f "$1" ]; then
      md5sum "$1" | cut -c1-10
   else
      echo "missing" >&2
      missing=$((missing + 1))
      echo "0"
   fi
}

while IFS= read -r line; do
   # href="x.css" / src="y.js" - local files only (no ':' in the URL)
   while [[ "$line" =~ (href|src)=\"([^\":]*\.(css|js))\" ]]; do
      attr="${BASH_REMATCH[1]}"; file="${BASH_REMATCH[2]}"
      h=$(hash_of "$file" 2>/dev/null)
      line="${line/${attr}=\"${file}\"/${attr}=\"${file}?v=${h}\"}"
   done
   printf '%s\n' "$line"
done < "$IN" > "$OUT"

echo "generated htdocs/$OUT (assets versioned by content hash)"
