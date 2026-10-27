#!/usr/bin/env bash
# Read-only: report negotiated link properties from sysfs without assuming a board model.
set -euo pipefail
shopt -s nullglob
found=0
for dev in /sys/bus/pci/devices/*; do
  [[ -r "$dev/vendor" && -r "$dev/class" ]] || continue
  [[ "$(< "$dev/vendor")" == 0x1002 ]] || continue
  klass="$(< "$dev/class")"
  [[ "$klass" == 0x03* ]] || continue
  found=1
  printf 'AMD_GPU_PCI address=%s\n' "$dev"
  for fld in current_link_speed current_link_width max_link_speed max_link_width; do
    if [[ -r "$dev/$fld" ]]; then
      printf '%s=%s\n' "$fld" "$(< "$dev/$fld")"
    else
      printf '%s=unavailable\n' "$fld"
    fi
  done
done
(( found )) || echo 'NO_AMD_PCI_GPU_DETECTED'
