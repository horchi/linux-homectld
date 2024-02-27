#!/bin/bash

printf '%s' "$(nmcli -f bssid,ssid,mode,chan,rate,signal,bars,security,active,in-use -t dev wifi)" | sed s/"[\]:"/"-"/g |\
  jq -sR 'split("\n") | map(split(":")) | map({"id": .[0],
                                               "network": .[1],
                                               "mode": .[2],
                                               "channel": .[3],
                                               "rate": .[4],
                                               "signal": .[5],
                                               "bars": .[6],
                                               "security": .[7],
                                               "active": .[8],
                                               "inuse": .[9]
                                             })' | json_pp -json_opt canonical,utf8
