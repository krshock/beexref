#!/bin/sh
# (Re)writes dist/SHA256SUMS over the release artifacts.
set -eu
. "$(dirname "$0")/common.sh"

release_checksums
echo "checksums: $(release_dist)/SHA256SUMS"
cat "$(release_dist)/SHA256SUMS"
