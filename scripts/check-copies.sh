#!/usr/bin/env bash
# Every file in scripts/copied-files.txt must be byte-identical to orbis-compat's.
#
#   scripts/check-copies.sh [--compat <dir>]
#
# --compat defaults to $ORBIS_COMPAT_DIR, which the setup-orbis action exports, so in CI this
# compares against THE TREE THE BUILD JUST USED rather than against whatever master happens to
# be. A copy checked against a different commit than the one linked is not a check.
#
# ⚠ WHY THIS EXISTS, MEASURED. The composite action that installs this toolchain was copied
# from sonic3air to RetroArch to OpenGothic. Nobody compared them again. By 2026-09-18 they
# were 317 lines against 332, with different cache keys and different SDK sanity lists, and
# the divergence was found by reading them side by side, not by any build failing. The same
# week, an OpenSSL workaround sat in three workflows and a C runtime shim in two repositories,
# one of them a stub where the other was a real implementation.
#
# Exit codes: 0 identical · 1 a copy differs or is missing · 2 usage · 4 INCOMPLETE, no
# orbis-compat to compare against - a laptop without a checkout gets a warning, never a pass.
set -euo pipefail

COMPAT="${ORBIS_COMPAT_DIR:-}"
while [ $# -gt 0 ]; do
  case "$1" in
    --compat) COMPAT="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,18p' "$0"; exit 2 ;;
    *) echo "!! unknown argument: $1" >&2; exit 2 ;;
  esac
done

HERE="$(cd "$(dirname "$0")/.." && pwd)"
LIST="$HERE/scripts/copied-files.txt"
[ -f "$LIST" ] || { echo "!! no $LIST" >&2; exit 2; }

if [ -z "$COMPAT" ] || [ ! -d "$COMPAT" ]; then
  echo "?? INCOMPLETE: no orbis-compat checkout (pass --compat <dir> or set ORBIS_COMPAT_DIR)."
  echo "   The copies were NOT checked. This is not a pass."
  exit 4
fi
COMPAT="$(cd "$COMPAT" && pwd)"

rc=0; n=0
while IFS= read -r f; do
  case "$f" in ''|'#'*) continue ;; esac
  n=$((n + 1))
  # `here -> there` when the two trees file it differently; a bare path means the same on both.
  case "$f" in
    *' -> '*) here_rel="${f%% -> *}"; there_rel="${f##* -> }" ;;
    *)        here_rel="$f";          there_rel="$f" ;;
  esac
  mine="$HERE/$here_rel"; theirs="$COMPAT/$there_rel"
  if [ ! -f "$mine" ];   then echo "!! $here_rel: missing HERE";                 rc=1; continue; fi
  if [ ! -f "$theirs" ]; then echo "!! $f: missing in orbis-compat - if it was deleted there, delete the line too"; rc=1; continue; fi
  if cmp -s "$mine" "$theirs"; then
    echo "ok $f"
  else
    echo "!! $f DIFFERS from orbis-compat"
    diff -u "$theirs" "$mine" | head -40 || true
    rc=1
  fi
done < "$LIST"

echo
if [ "$rc" -eq 0 ]; then
  echo "== $n copied file(s) identical to $COMPAT"
else
  echo "!! a copy has drifted. Fix the copy, or move the file out of copied-files.txt and say"
  echo "   in the commit message which repository owns it now."
fi
exit "$rc"
