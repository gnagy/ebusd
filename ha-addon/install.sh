#!/bin/sh
# Copies ha-addon/ebusd_dev/ (with the binaries build.sh put in bin/) to /addons/ebusd_dev/ on a Home Assistant
# host and reloads the store, so the local app local_ebusd_dev can be installed or rebuilt there.
#
# Usage: ha-addon/install.sh <ssh host>      e.g. ha-test

set -e

cd "$(dirname "$0")"
host="${1:?usage: install.sh <ssh host>}"
test -x ebusd_dev/bin/ebusd || { echo "no ebusd_dev/bin/ebusd: run build.sh first" >&2; exit 1; }
echo "copying ebusd_dev $(sed -n 's/^version: //p' ebusd_dev/config.yaml) to $host:/addons/ebusd_dev..."
COPYFILE_DISABLE=1 tar -C ebusd_dev --exclude '._*' -cf - . \
  | ssh "$host" 'rm -rf /addons/ebusd_dev && mkdir -p /addons/ebusd_dev && tar -xf - -C /addons/ebusd_dev 2>/dev/null && ha store reload'
