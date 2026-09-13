#!/bin/bash
# -----------------------------------------------------------------------------
# makeconfig.sh - 'make config': edit Make.user with 'dialog'
#
#  The variables and their defaults come from Make.user.template, the current
#  values from Make.user (if present). Variables with the value 1 are switches
#  (checklist), all others are entered in a form. An empty value writes the
#  variable commented out (like in the template).
# -----------------------------------------------------------------------------

TEMPLATE="Make.user.template"
USERFILE="Make.user"

if ! command -v dialog >/dev/null; then
   echo "'dialog' is required: apt install dialog"
   exit 1
fi

if [ ! -f "$TEMPLATE" ]; then
   echo "$TEMPLATE not found (run in the source directory)"
   exit 1
fi

declare -a ORDER=()          # variables in template order
declare -A KIND=()           # switch | value
declare -A DEFAULT=()        # value from the template
declare -A CURRENT=()        # value from Make.user ('' -> not set / commented)
declare -A ENABLED=()        # switches: 1 / 0
declare -A INTEMPLATE=()

# ---- template: order, kind and defaults

while IFS= read -r line; do
   if [[ "$line" =~ ^#?([A-Z_][A-Z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]]; then
      var="${BASH_REMATCH[1]}"; val="${BASH_REMATCH[2]}"
      ORDER+=("$var"); INTEMPLATE[$var]=1
      DEFAULT[$var]="$val"
      if [ "$val" == "1" ]; then KIND[$var]=switch; else KIND[$var]=value; fi
      [[ "$line" == \#* ]] && ENABLED[$var]=0 || ENABLED[$var]=1
      CURRENT[$var]=""
      [[ "$line" != \#* ]] && CURRENT[$var]="$val"
   fi
done < "$TEMPLATE"

# ---- Make.user: current values (overrides the template), extra variables are kept

declare -a EXTRA=()

if [ -f "$USERFILE" ]; then
   for var in "${ORDER[@]}"; do
      # active line wins, a commented one means 'off / unset'
      if grep -qE "^[[:space:]]*${var}[[:space:]]*=" "$USERFILE"; then
         CURRENT[$var]="$(grep -E "^[[:space:]]*${var}[[:space:]]*=" "$USERFILE" | tail -1 | sed -E 's/^[^=]*=[[:space:]]*//')"
         ENABLED[$var]=1
      else
         ENABLED[$var]=0
         [ "${KIND[$var]}" == "value" ] && CURRENT[$var]=""
      fi
   done

   while IFS= read -r line; do
      if [[ "$line" =~ ^([A-Z_][A-Z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]]; then
         [ -z "${INTEMPLATE[${BASH_REMATCH[1]}]}" ] && EXTRA+=("$line")
      fi
   done < "$USERFILE"
fi

# ---- dialog 1: switches

items=()
for var in "${ORDER[@]}"; do
   if [ "${KIND[$var]}" == "switch" ]; then
      [ "${ENABLED[$var]}" == "1" ] && state=on || state=off
      items+=("$var" "" "$state")
   fi
done

exec 3>&1
selected=$(dialog --backtitle "homectld - make config" --title "Services / specials" \
   --checklist "Space toggles, Enter continues" 0 0 0 "${items[@]}" 2>&1 1>&3)
rc=$?
exec 3>&-
[ $rc -ne 0 ] && { clear; echo "aborted, $USERFILE unchanged"; exit 1; }

for var in "${ORDER[@]}"; do
   [ "${KIND[$var]}" == "switch" ] && ENABLED[$var]=0
done
for var in $selected; do
   var="${var//\"/}"; ENABLED[$var]=1
done

# ---- dialog 2: values (form)

form=()
row=1
declare -a FORMVARS=()
for var in "${ORDER[@]}"; do
   if [ "${KIND[$var]}" == "value" ]; then
      val="${CURRENT[$var]}"
      [ -z "$val" ] && [ "${ENABLED[$var]}" == "1" ] && val="${DEFAULT[$var]}"
      form+=("$var" "$row" 1 "$val" "$row" 14 40 0)
      FORMVARS+=("$var")
      row=$((row + 1))
   fi
done

exec 3>&1
values=$(dialog --backtitle "homectld - make config" --title "Personalization" \
   --form "Empty = not set (written commented out). Defaults: see $TEMPLATE" 0 0 0 "${form[@]}" 2>&1 1>&3)
rc=$?
exec 3>&-
[ $rc -ne 0 ] && { clear; echo "aborted, $USERFILE unchanged"; exit 1; }

i=0
while IFS= read -r val; do
   var="${FORMVARS[$i]}"
   CURRENT[$var]="$val"
   [ -n "$val" ] && ENABLED[$var]=1 || ENABLED[$var]=0
   i=$((i + 1))
done <<< "$values"

# ---- write Make.user in the structure of the template

[ -f "$USERFILE" ] && cp "$USERFILE" "$USERFILE.bak"

{
   while IFS= read -r line; do
      if [[ "$line" =~ ^#?([A-Z_][A-Z0-9_]*)[[:space:]]*=[[:space:]]*(.*)$ ]]; then
         var="${BASH_REMATCH[1]}"
         if [ "${KIND[$var]}" == "switch" ]; then
            [ "${ENABLED[$var]}" == "1" ] && echo "$var = 1" || echo "#$var = 1"
         else
            if [ "${ENABLED[$var]}" == "1" ] && [ -n "${CURRENT[$var]}" ]; then
               echo "$var = ${CURRENT[$var]}"
            else
               echo "#$var = ${DEFAULT[$var]}"
            fi
         fi
      else
         echo "$line"
      fi
   done < "$TEMPLATE"

   if [ ${#EXTRA[@]} -gt 0 ]; then
      echo
      echo "# additional settings (not in $TEMPLATE)"
      echo
      for line in "${EXTRA[@]}"; do echo "$line"; done
   fi
} > "$USERFILE"

clear
echo "$USERFILE written (backup: $USERFILE.bak)"
echo
grep -vE '^\s*$' "$USERFILE"
