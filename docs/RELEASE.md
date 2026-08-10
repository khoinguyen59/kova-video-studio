# LA Studio release runbook

This runbook is for Windows x64 releases. Do not promote a release because a tag exists: the release is complete only when every required artifact and validation result below is attached to the exact tagged commit.

## Preconditions

- `main` is green at the intended commit and the working tree is clean.
- `LASTUDIO_VERSION` in `CMakeLists.txt` matches the intended `vMAJOR.MINOR.RELEASE.BUILD` tag. Each of the four internal version fields is one digit only; carry at `9` (`0.0.0.9` then `0.0.1.0`), never use a suffix-like field such as `0.0.2.40`.
- The previous stable release and its assets remain available for rollback.
- The release workflow has access to the approved code-signing capability and has no unreviewed secret changes.
- Every executable third-party payload passes its configured integrity and signature gate. In particular, do not build a release from an eSpeak NG MSI unless it has both the catalog SHA-256 and a valid Authenticode signature; the currently audited upstream `1.52.0` MSI is unsigned and is not release-eligible.
- A completed [RC test matrix](RC_TEST_MATRIX.md) is available for this commit.

## Configure SignPath once

The tag workflow fails closed until all of the following are configured in the
GitHub repository. Do this only after the SignPath project and signing policy
have been approved; never replace this integration with a PFX stored in the
repository or in an Actions secret.

- Install the SignPath GitHub App for this repository and configure the
  SignPath policy to accept only GitHub-hosted Windows release builds.
- Add `SIGNPATH_API_TOKEN` as a GitHub Actions secret with submit permission
  for the release signing policy.
- Add these non-secret repository variables: `SIGNPATH_ORGANIZATION_ID`,
  `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`,
  `SIGNPATH_APPLICATION_ARTIFACT_CONFIGURATION_SLUG`,
  `SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION_SLUG`, and
  `SIGNPATH_CERTIFICATE_SUBJECT`.
- Configure the application artifact configuration to accept the Actions ZIP
  containing exactly `LA-Studio-<version>.exe` and `LAStudioRuntimeHost.exe`; configure
  the installer artifact configuration for the Actions ZIP containing
  `LA-Studio-Setup.exe`. Both configurations must return the signed files with
  those original names.

The workflow submits artifacts through SignPath's GitHub connector, verifies
that every returned binary has `Valid` Authenticode status and the configured
certificate subject, and only then creates the installer hash and draft
release.

## Cut a draft release

1. Update the version, changelog/release notes, notices, and any compatibility notes.
2. Run the Windows CI suite and catalog generation gate on the release commit. Before creating a
   tag, use **Actions → Windows CI → Run workflow** with `main` selected. Its manual packaging
   rehearsal builds and validates the staged payload with `package.ps1 -SkipInstaller`; it does
   not create an installer, tag, or GitHub Release. Resolve any rehearsal failure before cutting
   the tag.
3. Create an annotated, signed tag and verify it locally. Stable tags use `vMAJOR.MINOR.RELEASE.BUILD`; preview tags use `vMAJOR.MINOR.RELEASE.BUILD-beta.N` (or `-alpha.N` / `-rc.N`). The numeric core must match `LASTUDIO_VERSION` and contains exactly four single-digit fields. Increment the fourth field for each internal build: `0.0.0.1` through `0.0.0.9`, then `0.0.1.0`:

   ```powershell
   git tag -s vMAJOR.MINOR.RELEASE.BUILD -m "LA Studio vMAJOR.MINOR.RELEASE.BUILD"
   git tag -v vMAJOR.MINOR.RELEASE.BUILD
   ```

4. Push the tag only after the verification succeeds. The release workflow must create a **draft** release; it must not publish directly. Preview suffixes are automatically marked as GitHub prereleases, so Stable clients do not select them while Beta clients can.
5. Confirm the draft contains, all built from the tagged SHA:

   - signed installer and signed `LA-Studio-<version>.exe` / `LAStudioRuntimeHost.exe`;
   - installer SHA-256 file (`SHA256SUMS`);
   - SBOM and toolchain manifest;
   - source archive and the license/notices payload;
   - PDB archive retained with restricted access as appropriate;
   - completed RC test matrix and release notes.

6. On the artifact runner, verify the signatures and hash before any manual installation:

   ```powershell
   signtool verify /pa /v .\LA-Studio-Setup.exe
   Get-FileHash .\LA-Studio-Setup.exe -Algorithm SHA256
   ```

   The hash must match `SHA256SUMS` exactly and the signature publisher must be the approved release signer.

## Validate the draft

Perform the clean-machine smoke test in Windows Sandbox or a fresh VM with no Visual Studio, VC++ redistributable, system FFmpeg, or eSpeak NG installed. The packaged FFmpeg runtime must satisfy the Dubbing flow without relying on `PATH`.

1. Install the draft artifact.
2. Start `LA-Studio-<version>.exe`, verify the main window and `app.log` are created.
3. Start and stop `LAStudioRuntimeHost.exe` through the application test path.
4. Exercise one downloaded model/runtime and one video-dubbing flow.
5. Verify eSpeak phoneme budgeting is active and the local API requires authentication.
6. Uninstall, selecting each documented data-removal option, and verify no application binaries or runtime-host process remain.

Record failures in the RC matrix. Do not waive security, signing, integrity, licensing, or clean-install failures.

## Promote

Only a release owner may promote a draft after all validation is checked off.

1. Review the draft assets against the list above and verify the exact tag again.
2. Publish the release notes with tested hardware classes, known limitations, upgrade notes, and any explicitly accepted risk with an owner and target version.
3. Promote the GitHub draft to the intended channel. Mark beta builds as prereleases so the stable updater does not select them.
4. Monitor the issue tracker and update channel after promotion; retain all previous release assets.

## Rollback and downgrade

Never delete a published release to recall it. Deleting breaks existing update paths and removes evidence needed to diagnose the incident.

### Before promotion

If a draft fails validation, keep it as a draft or delete the draft. No public rollback is needed.

### After promotion

1. Demote the faulty release to a prerelease or remove it from the stable update channel.
2. Publish an incident note describing affected versions, the safe target version, and whether user data is at risk.
3. Rehearse the downgrade on a clean VM:

   - install the faulty version;
   - install the prior signed installer over it;
   - confirm Add/Remove Programs lists exactly one LA Studio version;
   - confirm no stale newer files remain in the application directory;
   - launch the downgraded app and verify models, history, settings, and dubbing projects are readable.

4. If downgrade is not safe for a particular database/project schema, document it in the release notes and publish a fixed forward release instead. Do not represent an untested downgrade as supported.

## Source offer

Attach the corresponding source archive to every public release. The archive must identify the exact tag/commit and include build instructions, `LICENSE`, `THIRD-PARTY-NOTICES.md`, and the license payload. Keep release assets available for the lifetime stated in the project policy.
