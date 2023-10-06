#! /bin/bash

DIR=`dirname "$0"`
${DIR}/sysctl "$1" "$2" "$3" "fwpn"
