#!/usr/bin/env bash
# Print the CHANGELOG.md section body for a single version.
#
# Usage: changelog-extract.sh <version>      # e.g. 0.3.0  (no leading "v")
#
# Emits the lines between the "## [<version>] - <date>" heading and the next
# "## " heading, with surrounding blank lines trimmed. Exits non-zero if the
# version has no section, so a release can never publish empty notes.
set -euo pipefail

VERSION="${1:?usage: changelog-extract.sh <version>}"
VERSION="${VERSION#v}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CHANGELOG="${REPO_ROOT}/CHANGELOG.md"

[[ -f "${CHANGELOG}" ]] || { echo "ERROR: ${CHANGELOG} not found" >&2; exit 1; }

body="$(awk -v ver="${VERSION}" '
  $0 ~ "^## \\[" ver "\\]" { capture = 1; next }
  capture && /^## / { exit }
  capture { print }
' "${CHANGELOG}")"

# Trim leading/trailing blank lines.
body="$(printf '%s\n' "${body}" | sed -e '/./,$!d' -e ':a' -e '/^\n*$/{$d;N;ba}')"

if [[ -z "${body//[[:space:]]/}" ]]; then
  echo "ERROR: no CHANGELOG.md section for version ${VERSION}" >&2
  exit 1
fi

printf '%s\n' "${body}"
