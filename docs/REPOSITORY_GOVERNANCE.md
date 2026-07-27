# Repository governance and GitHub settings

This file records the repository settings a maintainer must apply in GitHub. Repository files can request review and run checks; they cannot enforce branch or tag protection on their own.

## Required protection for `main`

Configure a GitHub ruleset or branch-protection rule with these minimum controls:

- Require a pull request before merging, with at least one approval from a code owner.
- Require review from code owners and dismiss stale approvals after new commits.
- Require the `Build, test, and lint` check from the `Windows CI` workflow.
- Require conversation resolution, block force-pushes and deletions, and restrict direct pushes to release maintainers.
- Require signed commits where the repository's GitHub plan supports it.

## Release tags

- Create annotated, GPG- or SSH-signed tags only; verify the signature before pushing.
- Restrict `v*` tag creation to release maintainers through a tag ruleset where available.
- A tag creates only a draft release. Promotion remains a reviewed human action after RC evidence is attached.

## Ownership and dependency updates

- `CODEOWNERS` assigns release-critical files to the repository owner. Update it whenever maintainership changes.
- Dependabot is enabled for GitHub Actions. Review pins and release notes before merging an update; do not automatically merge workflow changes.
- The DCO check in Windows CI is required for every pull-request commit. The contributor procedure is in `CONTRIBUTING.md`.
