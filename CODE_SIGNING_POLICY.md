# Code signing policy

> **Status: not signed yet.** The portable Windows download is not code-signed yet. The [Microsoft Store edition](https://apps.microsoft.com/detail/9N1TJ9D5R5C8) is signed by Microsoft. The Mac app is signed and notarized by Apple. This page will be updated when signed portable releases begin.

## What is signed

The Windows program `Dropocalypse.exe` in the **Windows-portable** download on the [Releases page](https://github.com/allenrobberson/dropocalypse/releases). Every signed file is built automatically by GitHub Actions from the public source code in this repository ([build workflow](https://github.com/allenrobberson/dropocalypse/blob/main/.github/workflows/build.yml)), and every release is approved by hand before it is signed.

The product name in every signed file is **Dropocalypse**, and its version matches the release.

The in-app updater always checks the SHA-256 published in `SHA256SUMS.txt`. Once `Dropocalypse.exe` is signed, **Update now** also requires the downloaded copy to carry a trusted Authenticode signature from the same publisher. Until a signed build is what you're running, updates keep working on the checksum alone. The Mac updater requires the download to be signed with the same Developer ID team as the running app and accepted by Gatekeeper.

## Team roles

| Role | Members |
|---|---|
| **Committers and reviewers** | [Allen Robberson (@allenrobberson)](https://github.com/allenrobberson), see [repository collaborators](https://github.com/allenrobberson/dropocalypse/graphs/contributors) |
| **Approvers** | [Allen Robberson (@allenrobberson)](https://github.com/allenrobberson) (repository owner) |

Changes from anyone outside the team arrive as pull requests and are reviewed before they are merged. All team members use multi-factor authentication for GitHub.

## Privacy

Dropocalypse does not send any information to other networked systems unless the user asks for it, with one exception the user controls: it checks GitHub for new versions about once a day, and downloads an update only when the user clicks **Update now**. No personal information, audio or usage data is sent. The first launch on Windows explains this and offers to turn it off, and it can be switched off any time in the **Updates** menu. See the full [Privacy Policy](PRIVACY.md).

## Installing and uninstalling

Dropocalypse for Windows is portable: unzip it and run `Dropocalypse.exe`; nothing is installed. To uninstall, delete the Dropocalypse folder and `%APPDATA%\Dropocalypse`.
