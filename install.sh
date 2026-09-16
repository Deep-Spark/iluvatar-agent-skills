#!/usr/bin/env bash
# install.sh — register Iluvatar skills for AI coding agents.
# All entries are USER-level. Two paths are always installed together:
#
#   ~/.claude/skills/iluvatar-*/     (Claude Code)
#   ~/.agents/skills/iluvatar-*/     (Codex CLI / Cursor / GitHub Copilot —
#                                     .agents is the cross-client convention)
#
# Each is 6 directory symlinks pointing back into this repo, so editing
# SKILL.md takes effect immediately without re-running install.
#
# Usage:
#   bash install.sh              # install (both paths, 12 symlinks total)
#   bash install.sh --dry-run    # preview without writing
#   bash install.sh --uninstall  # remove all entries
#   bash install.sh --uninstall --dry-run
#
# VS Code users: Reload Window after first install for Copilot to pick up.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SKILLS=(
  iluvatar-ixrt
  iluvatar-ixjpeg
  iluvatar-ixcodec
  iluvatar-ixsys-guide
  iluvatar-ixkncli-guide
  iluvatar-ixobjdump-guide
)

DRY_RUN=0
UNINSTALL=0

usage() { sed -n '2,18p' "$0"; }

while [[ $# -gt 0 ]]; do
  case "$1" in
    --dry-run)   DRY_RUN=1; shift;;
    --uninstall) UNINSTALL=1; shift;;
    -h|--help)   usage; exit 0;;
    *)           echo "unknown arg: $1" >&2; usage; exit 1;;
  esac
done

for skill_name in "${SKILLS[@]}"; do
  [[ -f "$REPO/skills/$skill_name/SKILL.md" ]] || {
    echo "skills/$skill_name/SKILL.md not found at $REPO" >&2
    exit 1
  }
done

run() {
  printf '+ %s\n' "$*"
  [[ $DRY_RUN -eq 1 ]] || eval "$@"
}

# Best-effort cleanup of a legacy file/symlink. Failures (e.g. wrong owner)
# print a note and continue, instead of aborting via set -e.
rm_legacy() {
  local target="$1"
  [[ -e "$target" || -L "$target" ]] || return 0
  printf '+ rm -f (legacy) %s\n' "$target"
  [[ $DRY_RUN -eq 1 ]] && return 0
  rm -f "$target" 2>/dev/null || \
    printf '  (skipped: cannot remove %s — likely owned by another user)\n' "$target" >&2
}

install_all() {
  local claude_dir="$HOME/.claude/skills"
  local agents_dir="$HOME/.agents/skills"
  local claude_repo_rel agents_repo_rel
  claude_repo_rel="$(realpath -m --relative-to="$claude_dir" "$REPO")"
  agents_repo_rel="$(realpath -m --relative-to="$agents_dir" "$REPO")"

  echo "==> Claude Code     (~/.claude/skills/)"
  run "mkdir -p ~/.claude/skills"
  for s in "${SKILLS[@]}"; do
    run "ln -sfn '$claude_repo_rel/skills/$s' ~/.claude/skills/$s"
  done

  echo "==> Agents (.agents) (~/.agents/skills/)"
  echo "    covers Codex CLI, Cursor, GitHub Copilot Chat"
  run "mkdir -p ~/.agents/skills"
  for s in "${SKILLS[@]}"; do
    run "ln -sfn '$agents_repo_rel/skills/$s' ~/.agents/skills/$s"
  done

  # Clean up legacy paths from older script versions (skip silently if absent).
  for s in "${SKILLS[@]}"; do
    rm_legacy ~/.codex/skills/$s
    rm_legacy ~/.codex/prompts/$s.md
    rm_legacy ~/.cursor/skills/$s
    rm_legacy ~/.cursor/commands/$s.md
    rm_legacy ~/.copilot/skills/$s
  done

  # Remove unpublished or renamed skills from older installs.
  # iluvatar-adapt-base→iluvatar-cuda-base; iluvatar-cuda-base is unpublished.
  for s in iluvatar-adapt-base iluvatar-cuda-base; do
    rm_legacy ~/.claude/skills/$s
    rm_legacy ~/.agents/skills/$s
  done

  echo
  echo "VS Code: Ctrl+Shift+P -> 'Reload Window' after first install."
}

uninstall_all() {
  echo "==> Claude Code     (uninstall ~/.claude/skills/)"
  for s in "${SKILLS[@]}"; do run "rm -f ~/.claude/skills/$s"; done

  echo "==> Agents (.agents) (uninstall ~/.agents/skills/)"
  for s in "${SKILLS[@]}"; do run "rm -f ~/.agents/skills/$s"; done

  for s in iluvatar-adapt-base iluvatar-cuda-base; do
    rm_legacy ~/.claude/skills/$s
    rm_legacy ~/.agents/skills/$s
  done
}

if [[ $UNINSTALL -eq 1 ]]; then
  uninstall_all
else
  install_all
fi

echo
echo "Done. Hint: '--dry-run' to preview, '--uninstall' to revert."
