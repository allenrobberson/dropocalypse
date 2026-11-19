#!/usr/bin/env bash
# Make a GitHub release easy for non-technical people:
#   1. plain-English labels on every download (the file names stay the same,
#      so the in-app updater and old links keep working)
#   2. a "pick your computer and click" box at the top of the release notes
# Usage: scripts/release-friendly.sh 1.3.1      (needs GH_TOKEN and GITHUB_REPOSITORY)
set -euo pipefail

VER="${1#v}"
TAG="v$VER"
REPO="${GITHUB_REPOSITORY:-allenrobberson/dropocalypse}"
BASE="https://github.com/$REPO/releases/download/$TAG"
STORE="https://apps.microsoft.com/detail/9N1TJ9D5R5C8"

label_for() {
  case "$1" in
    *-macOS.dmg)               echo "Mac: click here to download" ;;
    *-Windows-portable.zip)    echo "Windows: click here to download (no install needed)" ;;
    *-Linux-x86_64.AppImage)   echo "Linux: click here to download (AppImage)" ;;
    *User-Guide.pdf)           echo "User Guide (PDF, printable)" ;;
    *-macOS.zip)               echo "Mac .zip (for the in-app updater; most people want the Mac download)" ;;
    *.pkg)                     echo "Mac App Store package (for Apple; you can ignore this)" ;;
    *-Store.msix)              echo "Microsoft Store package (ignore; use the Microsoft Store instead)" ;;
    *-Linux-x86_64.tar.gz)     echo "Linux as a plain folder (.tar.gz, for advanced users)" ;;
    SHA256SUMS.txt)            echo "Checksums (used by the in-app updater; you can ignore this)" ;;
    *)                         echo "" ;;
  esac
}

# 1. Labels
gh api "repos/$REPO/releases/tags/$TAG" --jq '.assets[] | "\(.id)\t\(.name)"' |
while IFS=$'\t' read -r id name; do
  label="$(label_for "$name")"
  [ -z "$label" ] && continue
  if gh api -X PATCH "repos/$REPO/releases/assets/$id" -f name="$name" -f label="$label" >/dev/null; then
    echo "labelled $name"
  else
    echo "::warning::could not label $name"
  fi
done

# 2. Download box at the top of the notes (added once)
MARK="<!-- easy-download -->"
body="$(gh release view "$TAG" --repo "$REPO" --json body --jq .body)"
if ! grep -qF "$MARK" <<<"$body"; then
  {
    echo "$MARK"
    echo "## ⬇️ Download Dropocalypse (free)"
    echo
    echo "**Pick your computer and click. That's it.**"
    echo
    echo "| | |"
    echo "|---|---|"
    echo "| 🍎 **Mac** | **[Click here to download for Mac]($BASE/Dropocalypse-$VER-macOS.dmg)**, open the file, then drag Dropocalypse into Applications. |"
    echo "| 🪟 **Windows** | **[Click here to get it from the Microsoft Store]($STORE)** (easiest, one-click install, no warnings). Or **[download the no-install version]($BASE/Dropocalypse-$VER-Windows-portable.zip)**. |"
    echo "| 🐧 **Linux** | **[Click here to download for Linux]($BASE/Dropocalypse-$VER-Linux-x86_64.AppImage)** |"
    echo "| 📖 **Help** | **[Download the User Guide (PDF)]($BASE/Dropocalypse-User-Guide.pdf)** |"
    echo
    echo "You don't need anything else on this page. The list of files at the bottom is for the auto-updater and advanced users."
    echo
    echo "---"
    echo
    printf '%s\n' "$body"
  } > /tmp/release-notes.md
  gh release edit "$TAG" --repo "$REPO" --notes-file /tmp/release-notes.md
  echo "added download box"
fi
