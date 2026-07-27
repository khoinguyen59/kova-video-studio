# Contributing to LA Studio

Thank you for improving LA Studio. Please open an issue before undertaking a large feature or a change that affects models, native runtimes, packaging, or licensing.

## Before opening a pull request

- Keep each pull request focused and explain the user-visible impact and verification performed.
- Build and run the relevant tests on Windows where possible. For catalog changes, run `python scripts/generate_catalog.py` and commit the resulting `data/catalog.json` only when it is intentionally changed.
- Do not add model, runtime, executable, or license payloads without pinned provenance and a license review.
- Never commit credentials, tokens, private installers, downloaded models, or build output.

## Developer Certificate of Origin (DCO)

Every commit submitted to this repository must include a `Signed-off-by` trailer. By signing off, you certify that you wrote the contribution or otherwise have the right to submit it under this project's AGPL-3.0-only license.

Create a signed-off commit with:

```powershell
git commit -s -m "Describe the change"
```

For an existing local commit, amend it with `git commit --amend -s`. The CI check validates every commit in a pull request. Do not add another person's sign-off without their authorization.

## Pull-request checklist

- [ ] The change is scoped and documented.
- [ ] Relevant tests and static checks pass, or limitations are explained.
- [ ] New user-facing strings are localizable.
- [ ] New dependencies, binaries, models, or generated files include provenance, hashes where applicable, and licensing information.
- [ ] Every commit has a DCO sign-off.
