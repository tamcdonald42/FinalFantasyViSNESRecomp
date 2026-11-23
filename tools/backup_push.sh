#!/usr/bin/env bash
# Mirror this repo's branches to the private GitHub remote as an offsite backup.
#
# The GitHub token on this machine has scopes `gist, read:org, repo` but not
# `workflow`, so any push whose history contains .github/workflows/release.yml is
# rejected ("refusing to allow an OAuth App to create or update workflow"). This
# script publishes a rewritten copy of each branch with that one path stripped;
# local history is untouched. Run `gh auth refresh -s workflow` (or regenerate the
# PAT with the workflow scope) and this script becomes a plain `git push`.
#
# Usage: tools/backup_push.sh [branch ...]      (default: main exp/c0-aot-roots)
set -euo pipefail
cd "$(dirname "$0")/.."

REMOTE=${REMOTE:-origin}
BRANCHES=${*:-"main exp/c0-aot-roots"}
ORIG=$(git rev-parse --abbrev-ref HEAD)
# Never come back to a leftover push-* branch: the loop below resets its own
# target branch, so running this from push-* silently publishes the wrong tree.
case "$ORIG" in
  push-*) ORIG=${BRANCHES%% *}; git checkout -q "$ORIG" ;;
esac

for br in $BRANCHES; do
  tmp="push-$(printf '%s' "$br" | tr '/' '-')"
  git checkout -q -B "$tmp" "$br"
  FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f \
    --index-filter 'git rm -q --cached --ignore-unmatch .github/workflows/release.yml' \
    -- "$tmp" > /dev/null 2>&1
  git push -q "$REMOTE" "$tmp:$br"
  echo "pushed $br"
  git checkout -q "$ORIG"
  git branch -D "$tmp" > /dev/null
  git for-each-ref --format='%(refname)' refs/original | xargs -r -n1 git update-ref -d
done
