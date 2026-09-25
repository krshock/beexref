#!/bin/sh
# Fetches the pinned SQLite amalgamation into third_party/sqlite3.
# A version bump is deliberate: update VERSION.txt, this script's
# variables and the hashes, then re-run the board round-trip tests.
set -eu

VERSION="3.53.4"
ARCHIVE_VERSION="3530400"
YEAR="2026"
URL="https://sqlite.org/${YEAR}/sqlite-amalgamation-${ARCHIVE_VERSION}.zip"
SHA256_C="b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189"
SHA256_H="919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d"

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
dest="$root/third_party/sqlite3"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

curl -fsSL -o "$tmp/sqlite.zip" "$URL"
unzip -q -j "$tmp/sqlite.zip" '*/sqlite3.c' '*/sqlite3.h' -d "$tmp"

printf '%s  %s\n' "$SHA256_C" "$tmp/sqlite3.c" | sha256sum -c - >/dev/null
printf '%s  %s\n' "$SHA256_H" "$tmp/sqlite3.h" | sha256sum -c - >/dev/null

mkdir -p "$dest"
cp "$tmp/sqlite3.c" "$tmp/sqlite3.h" "$dest/"
printf 'sqlite %s installed in %s\n' "$VERSION" "$dest"
