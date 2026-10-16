#!/usr/bin/env bash
source "$(dirname "$0")/common.sh"

status=0
check_file "$ROM" "$ROM_SHA1" || status=1
check_file "$BIOS" "$BIOS_SHA1" || status=1
exit "$status"
