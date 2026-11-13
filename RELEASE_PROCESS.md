# Manual Release Process Guide

This document describes the step-by-step manual release process for Chiaki webOS using AI assistance to analyze
commits, generate release notes, and trigger the GitHub Actions workflow via opencode (with `gh` CLI integration).

## Overview

The manual release process involves:

1. **AI-driven commit analysis** - Analyze commit history since last release
2. **AI-generated release notes** following strict formatting conventions
3. **Preview and review** - release notes shown before any actions taken
4. **Human review and approval** for quality control
5. **Workflow dispatch** - trigger GitHub Actions via `gh` CLI
6. **Automated build and release** - the workflow cross-builds libchiaki and the app with the webOS toolchain,
   packages the `.ipk` and publishes the GitHub Release with it and its checksum

## Prerequisites

- [opencode](https://opencode.ai/) installed
- [GitHub CLI (`gh`)](https://cli.github.com) installed and authenticated (`gh auth login`)
- Understanding of conventional commit patterns
- Familiarity with major.minor versioning (no patch versions)
- The release candidate checked on a TV (see [docs/TV-CHECKS.md](docs/TV-CHECKS.md)): the host tests and the
  simulator do not prove the hardware

## AI-Assisted Release Process

Use this prompt in opencode to handle the entire release process:

### Master Release Prompt

```
I need to create a new release for Chiaki webOS. Please:

STEP 1: ANALYZE COMMITS
- Use `gh` CLI or available tools to get the latest release tag
- Fetch all commits between that tag and current HEAD
- Analyze each commit for user-facing changes

STEP 2: GENERATE RELEASE NOTES
Create structured release notes with this EXACT format:

[Only if breaking changes exist - add a [!CAUTION] banner at the top:]

> [!CAUTION]
> **Brief summary of the breaking change and user impact.**
> Migration steps or workarounds if applicable.

### Breaking Changes
- Description focusing on user impact (abc1234)

### New Features
- Feature description emphasizing user benefit (abc1234)

### Improvements
- Improvement description with user impact (abc1234)

### Bug Fixes
- Fix description focusing on resolved user issue (abc1234)

REQUIREMENTS:
- Focus ONLY on user-facing changes and impact
- EXCLUDE: docs, build, ci, chore, refactor, test commits
- Use active voice, present tense
- Include commit short hashes (GitHub renders as links)
- Credit external contributors inline: (abc1234 by @username); never the repository owner
- Version logic: major.minor format only (no patch)
  - MINOR version (0.1 -> 0.2): New features, bug fixes, improvements
  - MAJOR version (0.2 -> 1.0): Breaking changes detected
- Show this preview BEFORE any actions

STEP 3: SHOW PREVIEW
Display the generated release notes and ask for approval before proceeding.

STEP 4: TRIGGER WORKFLOW (after approval)
Use `gh workflow run` to trigger the "Build and Release" workflow:

```bash
gh workflow run release.yml \
  -f release_tag="v[VERSION]" \
  -f release_notes="[generated content]" \
  -f draft=true \
  -f prerelease=false
```

Please start with Step 1 - analyze the commits and show me the preview.
```

## How It Works

opencode will:
1. **Analyze commits** since last release via `gh` CLI
2. **Generate release notes** with proper formatting and categorization
3. **Show preview** and ask for approval
4. **Trigger GitHub Actions workflow** with the release notes
5. The workflow **creates the tag**, then on a macOS arm64 runner installs the webOS SDK
   (`scripts/webos-sdk.sh`), **cross-builds libchiaki** and its static dependencies
   (`scripts/chiaki-lib-build-webos.sh`, cached per chiaki-ng commit), writes the version into `appinfo.json`
   (`v0.2` is `0.2.0`: webOS wants three numbers), **builds and packages** the app (`./build.sh`) and publishes the
   GitHub Release with `org.homebrew.chiakiwebos_<version>_arm.ipk` and its `.sha256`

The released `.ipk` is built from the tag, so its Corresponding Source (AGPL-3.0) is this repository at that tag;
the app's About panel points here.

## Features

- **Automatic filtering** of technical commits (docs, tests, CI, etc.)
- **User-focused** release notes with clear impact descriptions
- **Smart versioning** - minor for features/fixes, major for breaking changes
- **Preview before action** - human approval required
- **Full build pipeline** - toolchain, libchiaki, app, package and release all in one workflow

## Prereleases

Prereleases let you build and publish a test `.ipk` from a PR branch before merging.

### Triggering

**From the PR page:** Comment `/prerelease` on the PR. Only repository collaborators (OWNER, MEMBER, COLLABORATOR)
can trigger this.

**From the CLI:**
```bash
gh workflow run prerelease.yml -f pr_number=<number>
gh workflow run prerelease.yml -r <branch-name>   # a branch without a PR
```

### How it works

1. Resolves the PR number and head commit (or the branch)
2. Computes a tag based on the latest release: `v<base>-pr<number>.<sha>` (e.g. `v0.2-pr42.abc1234`), and an app
   version of `<base>.<run number>` so the TV sees it as newer than the release it builds on
3. Builds the `.ipk` the same way as a release
4. Publishes a GitHub prerelease with the `.ipk` and checksum attached
5. Posts a comment on the PR with the release link (when triggered via `/prerelease`)

### Notes

- No CI gate - trigger whenever you want a test build
- The base version comes from the latest non-prerelease GitHub release (falls back to `v0.0`)
- Prerelease tags and releases are not cleaned up automatically

## Troubleshooting

- **gh CLI issues**: Run `gh auth status` to verify authentication
- **Workflow dispatch failed**: Check repository permissions for workflow dispatch
- **Invalid release notes**: Review format requirements and regenerate
- **Toolchain download fails**: The pinned webosbrew releases are in `scripts/webos-sdk.sh`
- **libchiaki build fails**: Ensure `scripts/chiaki-lib-build-webos.sh` works locally first; a stale cache entry is
  keyed on the chiaki-ng commit and the script, so changing either rebuilds it
- **App build fails**: Ensure `./build.sh` builds with zero warnings locally first

---

*Use the master prompt above to start your next release.*
