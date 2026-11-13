# Manual Release Process Guide

This document describes the step-by-step manual release process for Chiaki webOS using AI assistance to analyze
commits, generate release notes, and trigger the GitHub Actions workflow via opencode (with `gh` CLI integration).

## Overview

The manual release process involves:

1. **AI-driven commit analysis** - Analyze commit history since last release
2. **AI-generated release notes** following strict formatting conventions, committed as `release-notes/<tag>.md`
3. **Preview and review** - release notes shown before any actions taken
4. **Human review and approval** for quality control
5. **Notes committed first** - the tag carries its notes, so the release page is the reviewed file
6. **Workflow dispatch** - trigger GitHub Actions via `gh` CLI with the tag alone
7. **Automated build and release** - the workflow cross-builds libchiaki and the app with the webOS toolchain,
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
Draft release-notes/v[VERSION].md with this EXACT format:

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
Display the file's content and ask for approval before proceeding.

STEP 4: COMMIT THE NOTES, THEN TRIGGER THE WORKFLOW (after approval)
1. Commit release-notes/v[VERSION].md (the release workflow refuses a tag whose notes are not in the
   repository, so this has to land on the branch you dispatch from)
2. Trigger the "Build and Release" workflow with the tag alone:

```bash
git push origin main
gh workflow run release.yml \
  -f release_tag="v[VERSION]" \
  -F draft=false \
  -F prerelease=false
```

Please start with Step 1 - analyze the commits and show me the preview.
```

## How It Works

opencode will:
1. **Analyze commits** since last release via `gh` CLI
2. **Draft `release-notes/<tag>.md`** with proper formatting and categorization
3. **Show preview** and ask for approval
4. **Commit the notes** and **trigger the GitHub Actions workflow** with the tag alone
5. The workflow **refuses a tag with no notes**, then **creates the tag**, and on a macOS arm64 runner installs the
   webOS SDK (`scripts/webos-sdk.sh`), **cross-builds libchiaki** and its static dependencies
   (`scripts/chiaki-lib-build-webos.sh`, cached per chiaki-ng commit), writes the version into `appinfo.json`
   (`v0.2` is `0.2.0`: webOS wants three numbers), **builds and packages** the app (`./build.sh`) and publishes the
   GitHub Release with `com.renjfk.chiakiwebos_<version>_arm.ipk`, its `.sha256` and `com.renjfk.chiakiwebos.manifest.json`

The release page's body is `release-notes/<tag>.md` read from the tagged commit, not an input: what was reviewed in
the pull request is what the release says.

### The notes are part of the repository

`release-notes/<tag>.md` holds one file per published release, named exactly like its tag (`v0.1.0.md`,
`v0.1.0-rc1.md`). They are the record of what shipped, so they are committed, never edited after publishing. The
release candidate is no exception: it needs its notes before the run too. Only the PR prereleases
(`prerelease.yml`) skip this, since their notes are generated from the PR number.

The released `.ipk` is built from the tag, so its Corresponding Source (AGPL-3.0) is this repository at that tag;
the app's About panel points here.

### Listing in Dev Manager and the Homebrew Channel

`repo.webosbrew.org` is what Dev Manager's installable apps and the Homebrew Channel read, and it is generated from
[webosbrew/apps-repo](https://github.com/webosbrew/apps-repo). A release carries the manifest that index fetches:

```json
{ "id": "com.renjfk.chiakiwebos", "version": "0.1.0", "ipkUrl": "com.renjfk.chiakiwebos_0.1.0_arm.ipk", ... }
```

so the entry in apps-repo points at a URL that always serves the newest one:
`https://github.com/renjfk/chiaki-webos/releases/latest/download/com.renjfk.chiakiwebos.manifest.json`. That
`latest` URL only starts working with the first release that is not a pre-release, so check a candidate through its
own tag (`/releases/download/v0.1.0-rc2/...`). Installing an `.ipk` by hand (Dev Manager's install button or
`scripts/tv.sh install`) needs none of this: installed apps are listed from the TV itself.

### Release candidates

Tag a run `v<major>.<minor>[.<patch>]-rc<N>` (e.g. `v0.1.0-rc1`) to publish a release candidate: it builds the
version it is a candidate for (`0.1.0`) and the release is always marked as a pre-release, whatever the
`prerelease` input says. The tag keeps the final `v0.1.0` free for the release itself, and prerelease builds ignore
candidates when they pick the base release they build on, since a candidate is a pre-release.

## Features

- **Automatic filtering** of technical commits (docs, tests, CI, etc.)
- **User-focused** release notes with clear impact descriptions
- **Notes in the repository** - a release cannot publish without `release-notes/<tag>.md` at the tag
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
4. Publishes a GitHub prerelease with the `.ipk`, its checksum and the package manifest, with generated notes
5. Posts a comment on the PR with the release link (when triggered via `/prerelease`)

### Notes

- No CI gate - trigger whenever you want a test build
- The base version comes from the latest non-prerelease GitHub release (falls back to `v0.0`)
- Prerelease tags and releases are not cleaned up automatically

## Troubleshooting

- **gh CLI issues**: Run `gh auth status` to verify authentication
- **Workflow dispatch failed**: Check repository permissions for workflow dispatch
- **`release-notes/<tag>.md is missing`**: commit the notes for that tag on the branch you dispatch from, then run
  it again; the workflow refuses to tag without them
- **Invalid release notes**: Review format requirements and regenerate the file
- **Toolchain download fails**: The pinned webosbrew releases are in `scripts/webos-sdk.sh`
- **libchiaki build fails**: Ensure `scripts/chiaki-lib-build-webos.sh` works locally first; a stale cache entry is
  keyed on the chiaki-ng commit and the script, so changing either rebuilds it
- **App build fails**: Ensure `./build.sh` builds with zero warnings locally first

---

*Use the master prompt above to start your next release.*
