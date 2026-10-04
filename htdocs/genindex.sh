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

# the build id of the page (meta 'webif-build'): hash of the generated content, so it
# changes with any of the versioned files; main.js compares it with the current
# index.html on every connect and reloads the page on a difference

build=$(md5sum "$OUT" | cut -c1-10)
sed -i "s/@BUILD@/${build}/" "$OUT"

echo "generated htdocs/$OUT (assets versioned by content hash, build ${build})"
