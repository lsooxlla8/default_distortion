#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 <version> <artefacts-directory> <output-directory>" >&2
  exit 2
fi

version=$1
artefacts_directory=$2
output_directory=$3
project_root=$(cd "$(dirname "$0")/../.." && pwd)
work_directory=$(mktemp -d)
payload_directory="$work_directory/payload"
trap 'rm -rf "$work_directory"' EXIT

component="$artefacts_directory/AU/default_distortion.component"
vst3="$artefacts_directory/VST3/default_distortion.vst3"
standalone="$artefacts_directory/Standalone/default_distortion.app"
for required in "$component" "$vst3" "$standalone"; do
  if [[ ! -e "$required" ]]; then
    echo "missing build artefact: $required" >&2
    exit 1
  fi
done

mkdir -p \
  "$payload_directory/Library/Audio/Plug-Ins/Components" \
  "$payload_directory/Library/Audio/Plug-Ins/VST3" \
  "$payload_directory/Applications" \
  "$payload_directory/Library/Application Support/default_audio/default_distortion" \
  "$output_directory"

ditto "$component" \
  "$payload_directory/Library/Audio/Plug-Ins/Components/default_distortion.component"
ditto "$vst3" \
  "$payload_directory/Library/Audio/Plug-Ins/VST3/default_distortion.vst3"
ditto "$standalone" \
  "$payload_directory/Applications/default_distortion.app"

documentation_directory="$payload_directory/Library/Application Support/default_audio/default_distortion"
cp \
  "$project_root/BUILDING.md" \
  "$project_root/LICENSE.md" \
  "$project_root/README.md" \
  "$project_root/THIRD_PARTY_NOTICES.md" \
  "$documentation_directory/"
cp -R "$project_root/LICENSES" "$documentation_directory/LICENSES"

installer="$output_directory/default_distortion-$version-macOS-universal.pkg"
pkgbuild \
  --root "$payload_directory" \
  --identifier com.icanseesounds.defaultdistortion.pkg \
  --version "$version" \
  --install-location / \
  --ownership recommended \
  "$installer"

echo "$installer"
