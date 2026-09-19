#!/usr/bin/env bash
# Apply this kit's patch series to a checkout, or check that they still apply.
#
#   orbis-patch.sh apply  <family>/<project> <srcdir>   apply every *.patch, in name order
#   orbis-patch.sh check  <family>/<project>            clone SERIES' ref shallowly, dry-run
#   orbis-patch.sh check  --all                         every series in the registry
#   orbis-patch.sh list   <family>/<project>            print the series as this script reads it
#
# The registry is $ORBIS_KIT_DIR/patches, or the directory two above this script.
#
# ⚠ EXIT CODES ARE THE INTERFACE: 0 applied or clean, 1 a patch would not apply, 2 usage or a
# missing series, 3 the network or a clone failed. A harness must be able to tell "this patch is
# stale" from "GitHub was down", because the first is a decision for a person and the second is a
# retry. build-cores.sh's own sweep learned that distinction the hard way; see its exit convention.
#
# SPDX-License-Identifier: MIT
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
KIT="${ORBIS_KIT_DIR:-$(cd "$HERE/.." && pwd -P)}"
REG="$KIT/patches"
[[ -d "$REG" ]] || { echo "orbis-patch: no patch registry at $REG" >&2; exit 2; }

log(){  printf '\033[1;34m==>\033[0m %s\n' "$*"; }
ok(){   printf '   \033[1;32mok\033[0m   %s\n' "$*"; }
err(){  printf '\033[1;31mXX\033[0m %s\n' "$*" >&2; }

# ⚠ ONE PARSER, USED BY EVERY MODE. A second reader of SERIES would drift from this one, which is
# the disease this whole kit exists to treat - the composite action reached 332 lines against 317
# in two repositories before anyone compared them.
series_get(){ # <series file> <key>
  sed -n "s/[[:space:]]*#.*//; s/^$2:[[:space:]]*//p" "$1" | sed '/^$/d' | head -1
}
series_patches(){ # <series file> -> patch file names in order
  sed -n 's/[[:space:]]*#.*//; s/^patch:[[:space:]]*//p' "$1" | sed '/^$/d'
}

resolve(){ # <family>/<project>
  local d="$REG/$1"
  [[ -d "$d" ]] || { err "no such series: $1 (looked in $d)"; exit 2; }
  [[ -f "$d/SERIES" ]] || { err "$1 has no SERIES file"; exit 2; }
  printf '%s\n' "$d"
}

cmd_list(){
  local d; d="$(resolve "$1")"
  printf 'upstream: %s\nref: %s\npatches:\n' "$(series_get "$d/SERIES" upstream)" "$(series_get "$d/SERIES" ref)"
  series_patches "$d/SERIES" | sed 's/^/  /'
}

cmd_apply(){
  local d src n=0; d="$(resolve "$1")"; src="$2"
  [[ -d "$src" ]] || { err "not a directory: $src"; exit 2; }
  # ⚠ NAME ORDER FROM SERIES, NOT FROM THE GLOB. A patch present in the directory and absent from
  # SERIES is a patch nobody declared - most likely a *.parked one that was renamed, or a leftover.
  # The glob would apply it; this does not.
  while read -r p; do
    [[ -n "$p" ]] || continue
    [[ -f "$d/$p" ]] || { err "$1: SERIES names $p and it is not there"; exit 2; }
    if git -C "$src" apply --whitespace=nowarn "$d/$p"; then
      n=$((n+1))
    else
      err "$1: $p would not apply to $src"; exit 1
    fi
  done < <(series_patches "$d/SERIES")
  ok "$1: $n patch(es) applied to $src"
}

cmd_check_one(){
  local d up ref tmp rc=0; d="$(resolve "$1")"
  up="$(series_get "$d/SERIES" upstream)"; ref="$(series_get "$d/SERIES" ref)"
  [[ -n "$up" && -n "$ref" ]] || { err "$1: SERIES has no upstream/ref"; return 2; }
  tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' RETURN
  log "$1: $up @ $ref"
  # ⚠ SUBMODULES ARE OPT-IN PER SERIES, AND SKIPPING THEM LOOKS EXACTLY LIKE A STALE PATCH.
  # MEASURED 2026-09-19: with no submodules, three of Play!'s five patches reported
  # "deps/Framework/src/PathUtils.cpp: No such file or directory" against master 83700b2 - which
  # reads as upstream having moved the file, when in truth the file had never been cloned. Play!
  # keeps Framework, CodeGen and Dependencies as submodules and its patches reach inside them.
  # Shallow by default because a check that took a full clone of Play! is a check nobody runs.
  local sm; sm="$(series_get "$d/SERIES" submodules)"
  local clone_args=(--depth 1 --branch "$ref")
  if [[ "$sm" == recursive ]]; then
    clone_args+=(--recurse-submodules --shallow-submodules)
  else
    clone_args+=(--recurse-submodules=no)
  fi
  if ! git clone -q "${clone_args[@]}" "$up" "$tmp/src" 2>"$tmp/clone.log"; then
    err "$1: clone failed - $(tail -1 "$tmp/clone.log")"; return 3
  fi
  local sha; sha="$(git -C "$tmp/src" rev-parse --short HEAD)"
  while read -r p; do
    [[ -n "$p" ]] || continue
    # --check is a dry run; sequential state matters, so apply for real as we go.
    if git -C "$tmp/src" apply --whitespace=nowarn "$d/$p" 2>"$tmp/apply.log"; then
      ok "$p"
    else
      err "$1: $p does not apply to $ref ($sha) - $(head -1 "$tmp/apply.log")"; rc=1
    fi
  done < <(series_patches "$d/SERIES")
  [[ $rc -eq 0 ]] && ok "$1: whole series applies to $ref ($sha)"
  return $rc
}

cmd_check_all(){
  local rc=0 s
  while read -r s; do
    cmd_check_one "$s" || { [[ $? -eq 3 ]] && rc=${rc} || rc=1; }
  done < <(cd "$REG" && find . -name SERIES -print | sed 's|^\./||; s|/SERIES$||' | LC_ALL=C sort)
  return $rc
}

case "${1:-}" in
  apply) [[ $# -eq 3 ]] || { err "usage: orbis-patch.sh apply <family>/<project> <srcdir>"; exit 2; }
         cmd_apply "$2" "$3" ;;
  check) if [[ "${2:-}" == --all ]]; then cmd_check_all; else
           [[ $# -eq 2 ]] || { err "usage: orbis-patch.sh check <family>/<project>|--all"; exit 2; }
           cmd_check_one "$2"; fi ;;
  list)  [[ $# -eq 2 ]] || { err "usage: orbis-patch.sh list <family>/<project>"; exit 2; }
         cmd_list "$2" ;;
  *)     sed -n '2,14p' "$0"; exit 2 ;;
esac
