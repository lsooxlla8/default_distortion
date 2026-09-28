#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <version> <staged-package-directory> <output-directory>" >&2
  exit 2
fi

version=$1
staged_directory=$2
output_directory=$3
script_directory=$(cd "$(dirname "$0")" && pwd)
installer="$output_directory/default_distortion-$version-Linux-x64.run"
archive=$(mktemp)
trap 'rm -f "$archive"' EXIT

for required in \
  "$staged_directory/VST3/default_distortion.vst3" \
  "$staged_directory/LV2/default_distortion.lv2" \
  "$staged_directory/Standalone/default_distortion"; do
  if [[ ! -e "$required" ]]; then
    echo "missing build artefact: $required" >&2
    exit 1
  fi
done

mkdir -p "$output_directory"
tar -C "$staged_directory" -czf "$archive" .
cp "$script_directory/installer-header.sh" "$installer"
cat "$archive" >> "$installer"
chmod 0755 "$installer"
echo "$installer"
