#!/bin/sh
set -eu

usage() {
  printf '%s\n' \
    'default_distortion installer' \
    '' \
    'usage: default_distortion installer [--user|--system|--help]' \
    '' \
    '  --user    install for the current user (default)' \
    '  --system  install under /usr/local; run as root' \
    '  --help    show this help'
}

mode=user
case "${1:-}" in
  ''|--user) mode=user ;;
  --system) mode=system ;;
  --help|-h) usage; exit 0 ;;
  *) usage >&2; exit 2 ;;
esac

if [ "$mode" = system ]; then
  if [ "$(id -u)" -ne 0 ]; then
    echo 'system installation requires root; rerun with sudo or use --user' >&2
    exit 1
  fi
  vst3_directory=/usr/local/lib/vst3
  lv2_directory=/usr/local/lib/lv2
  binary_directory=/usr/local/bin
  documentation_directory=/usr/local/share/default_audio/default_distortion
else
  vst3_directory="${HOME}/.vst3"
  lv2_directory="${HOME}/.lv2"
  binary_directory="${HOME}/.local/bin"
  documentation_directory="${XDG_DATA_HOME:-${HOME}/.local/share}/default_audio/default_distortion"
fi

temporary_directory=$(mktemp -d)
trap 'rm -rf "$temporary_directory"' EXIT HUP INT TERM
payload_line=$(awk '/^__DEFAULT_DISTORTION_PAYLOAD__$/ { print NR + 1; exit }' "$0")
tail -n "+$payload_line" "$0" | tar -xz -C "$temporary_directory"

mkdir -p \
  "$vst3_directory" \
  "$lv2_directory" \
  "$binary_directory" \
  "$documentation_directory"
rm -rf \
  "$vst3_directory/default_distortion.vst3" \
  "$lv2_directory/default_distortion.lv2"
cp -a "$temporary_directory/VST3/default_distortion.vst3" "$vst3_directory/"
cp -a "$temporary_directory/LV2/default_distortion.lv2" "$lv2_directory/"
install -m 0755 \
  "$temporary_directory/Standalone/default_distortion" \
  "$binary_directory/default_distortion"
cp \
  "$temporary_directory/BUILDING.md" \
  "$temporary_directory/LICENSE.md" \
  "$temporary_directory/README.md" \
  "$temporary_directory/THIRD_PARTY_NOTICES.md" \
  "$documentation_directory/"
rm -rf "$documentation_directory/LICENSES"
cp -a "$temporary_directory/LICENSES" "$documentation_directory/LICENSES"

printf '%s\n' \
  'default_distortion installed:' \
  "  VST3: $vst3_directory/default_distortion.vst3" \
  "  LV2:  $lv2_directory/default_distortion.lv2" \
  "  App:  $binary_directory/default_distortion"
exit 0
__DEFAULT_DISTORTION_PAYLOAD__
