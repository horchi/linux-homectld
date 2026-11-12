#!/bin/bash
# -----------------------------------------------------------------------------
# genrepo.sh - provide the documentation of the source tree for the README viewer
#
#  All markdown files of the repository (git tracked, without git all found ones)
#  and the files they link relatively (images, config examples, ...) are linked
#  as symlinks to htdocs/repo/<path>, so the viewer opens them in the WEBIF and
#  not at GitHub. README.md and doc/ are linked directly in htdocs, not here.
#  'make install' copies the targets of the symlinks (cp -L) to WEBDEST.
#  Called on every make (htdocs/Makefile, target index.html), repo/ is not in git.
# -----------------------------------------------------------------------------

cd "$(dirname "$0")" || exit 1

HTDOCS=$(pwd)
ROOT=$(cd ..; pwd)
DEST="$HTDOCS/repo"

cd "$ROOT" || exit 1

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
   mdFiles=$(git ls-files '*.md')
else
   mdFiles=$(find . -name '*.md' -type f | sed 's#^\./##')
fi

files=()

for f in $mdFiles; do
   case "$f" in
      README.md|doc/*|htdocs/*) continue ;;
   esac

   files+=("$f")
done

# relative links and images of all markdown files (also of README.md and doc/*.md)

for f in $mdFiles; do
   case "$f" in
      htdocs/*) continue ;;
   esac

   dir=$(dirname "$f")

   while read -r link; do
      link="${link%%#*}"                   # strip anchor
      link="${link%% *}"                   # strip title ("...")
      [ -z "$link" ] && continue

      case "$link" in
         *:*|/*) continue ;;               # http:, mailto:, absolute
      esac

      path=$(realpath -m --relative-to="$ROOT" "$ROOT/$dir/$link")

      case "$path" in
         ../*|README.md|doc/*|htdocs/*|*.md) continue ;;
      esac

      [ -f "$path" ] && files+=("$path")
   done < <(grep -oE '\]\([^)]+\)|src="[^"]+"' "$f" | sed -E 's/^\]\(//; s/\)$//; s/^src="//; s/"$//')
done

rm -rf "$DEST"

for f in $(printf '%s\n' "${files[@]}" | sort -u); do
   mkdir -p "$DEST/$(dirname "$f")"
   ln -sr "$ROOT/$f" "$DEST/$f"
done

exit 0
