#!/bin/sh

CPUPATH=/sys/devices/system/cpu

MAXID=$(cat $CPUPATH/present | awk -F- '{print $NF}')

set_governor() {
  echo "Setting CPU frequency governor to \"$1\""
  i=0
  while [ "$i" -le "$MAXID" ]; do
    echo "$1" > "$CPUPATH/cpu$i/cpufreq/scaling_governor"
    i=$((i + 1))
  done
}

case "$1" in
  fast | performance)
    set_governor "performance"
    ;;
  *)
    echo "Usage: $0 fast"
    exit 1
    ;;
esac
