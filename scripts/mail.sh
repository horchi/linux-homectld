#!/bin/bash


# -----------------------------------------
# require: mail of 'GNU Mailutils' package
#
# $1 subject, $2 body, $3 content type, $4 receiver(s)

subject="$1"

# a subject with non ASCII characters (umlauts, emoji) has to be encoded (RFC 2047),
# otherwise the mail clients show the raw UTF-8 bytes as Latin-1 ('STÃRUNG')

if LC_ALL=C grep -q '[^ -~]' <<< "$subject"; then
   subject="=?UTF-8?B?$(printf '%s' "$subject" | base64 -w0)?="
fi

echo "$2" | mail -s "$subject" -a "Content-Type: $3; charset=UTF-8" $4
