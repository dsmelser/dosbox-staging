# Getting the fork's code reviewed

Copilot and other GitHub reviewers only review pull requests. This fork
(`dsmelser/dosbox-staging`) has no automatic reviewer set up, so this page
shows how to open a review-only pull request inside the fork and ask for a
review.

The launcher work lives on the `settings-launcher` branch, which branches off
upstream commit `23055256f`.

## Prerequisites

- A Copilot plan that includes code review (Pro, Pro+, Max, Business or
  Enterprise) if you want Copilot to review.
- The GitHub CLI (`gh`), logged in as the fork's owner.
- Git remotes: `origin` is the fork, `upstream` is
  `dosbox-staging/dosbox-staging`.

## 1. Review the changes on a branch

Create a base branch that points at the commit the work started from, then
open a pull request from the work branch into it. The pull request shows only
our commits, even after the fork's `main` gets synced with upstream.

```
git fetch upstream
git push origin "$(git merge-base origin/settings-launcher upstream/main)":refs/heads/review-base
gh pr create -R dsmelser/dosbox-staging --base review-base --head settings-launcher \
    --title "Review: settings launcher" --body "Review-only pull request; don't merge."
```

As long as the fork's `main` hasn't been synced with upstream, using
`--base main` gives the same diff without the extra branch.

## 2. Review whole files that already exist

Reviewers mostly comment on changed lines. To get complete existing files
reviewed (for example, after the work has been merged), use a base branch
from which those files are deleted, so the pull request shows them as new:

```
git switch -c review-base-full settings-launcher
git rm -r --quiet src/platform/windows/launcher tests/launcher_tests.cpp
git commit -m "Remove the launcher files for a full review"
git push origin review-base-full
git switch settings-launcher
gh pr create -R dsmelser/dosbox-staging --base review-base-full --head settings-launcher \
    --title "Full review: settings launcher" --body "Review-only pull request; don't merge."
```

Adjust the deleted paths to the files you want reviewed.

## 3. Ask Copilot to review

- **Once:** on the pull request page, choose **Copilot** under
  **Reviewers**. Request it again the same way after pushing fixes.
- **Automatically, for this repository:** Settings → Rules → Rulesets → New
  ruleset → New branch ruleset. Set Enforcement to Active, add the target
  branches (for example `review-base*`), select **Automatically request
  Copilot code review** and optionally **Review new pushes** and **Review
  draft pull requests**. This fork has such a ruleset for `review-base*`,
  with **Review new pushes** on.
- **Automatically, for every pull request you open:** Copilot settings →
  Code review → **Automatic Copilot code review**.

Copilot reads review guidance from the pull request's head branch
(`copilot-instructions.md`, `*.instructions.md`, `AGENTS.md`, `REVIEW.md`,
`CLAUDE.md`). The project's coding rules are in `.claude/rules/`, which
Copilot doesn't read, and Copilot doesn't follow links, so
`.github/copilot-instructions.md` holds a copy of the C++ rules from
`.claude/rules/code-style.md`. Update both together.

## 4. Handle the feedback and clean up

1. Push fixes to the head branch; the pull request updates by itself.
2. When done, close the pull request without merging it.
3. Delete the base branch.

```
gh pr close <number> -R dsmelser/dosbox-staging
git push origin --delete review-base
```

## Reviews without a pull request

- Claude Code: run `/code-review` in the repository to review the current
  branch's changes locally.
- CodeRabbit, which reviews upstream's pull requests, can be installed on the
  fork as a GitHub App.
