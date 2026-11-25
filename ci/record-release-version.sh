#!/usr/bin/env bash
# Writes a published release's version to VERSION and pushes it, so builds that aren't releases name it without asking git.
# Usage: ci/record-release-version.sh <tag>, run in a checkout of the branch to write to.
set -euo pipefail
cd "$(dirname "$0")/.."

version="${1#v}"
current="$(head -n 1 VERSION)"
# Only forward: a fix released for an older version mustn't make newer work look older.
if [ "$(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -n 1)" = "$current" ]; then
  echo "VERSION is $current already, not older than $version"
  exit 0
fi

echo "$version" > VERSION
git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git commit -m "Set VERSION to $version, released as $1" VERSION
git push
