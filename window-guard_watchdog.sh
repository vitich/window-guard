#!/bin/bash

while true; do
    if ! pgrep -x window-guard >/dev/null; then
        echo "$(date): window-guard not running, starting..."
        /usr/bin/window-guard &
    fi
    sleep 5
done
