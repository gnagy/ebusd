#!/bin/sh
# Builds ebusd from this checkout on proton (amd64, Docker) and copies the binaries out for the add-on.
# Nothing is installed on proton and nothing is pushed: the image stays in proton's Docker as
# ebusd-dev:<tag>, and the build stage's toolchain is only in its build cache.
#
# Usage: ha-addon/build.sh [<tag>]      default: the short commit, with -dirty for uncommitted changes
#        BUILD_HOST=<ssh host>          default proton

set -e

cd "$(dirname "$0")/.."
host="${BUILD_HOST:-proton}"
tag="${1:-$(git rev-parse --short HEAD)$(git diff --quiet HEAD -- . ':!ha-addon' || echo -dirty)}"
image="ebusd-dev:$tag"
export DOCKER_HOST="ssh://$host"

echo "building $image on $host..."
docker build --platform linux/amd64 -f ha-addon/Dockerfile.build -t "$image" .

out=ha-addon/ebusd_dev/bin
mkdir -p "$out"
container=$(docker create "$image")
trap 'docker rm -f "$container" >/dev/null' EXIT
docker cp "$container:/usr/bin/ebusd" "$out/ebusd"
docker cp "$container:/usr/bin/ebusctl" "$out/ebusctl"
echo "$tag" > "$out/VERSION"
echo "built $image; binaries in $out"
