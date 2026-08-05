# AI agent response - Spleeter Colab tunnel bootstrap repair

Date: 2026-08-06

## Root cause

The exact Spleeter CUDA worker had already passed its startup probe. The
failure happened immediately afterwards: a fresh Colab runtime has no
`cloudflared` executable, and the launcher called it through
`subprocess.run()` without catching `FileNotFoundError`. The download/install
branch therefore never ran.

## Source repair

- Added a safe `cloudflared_ready()` probe that treats a missing binary as not
  installed.
- Kept the existing official Cloudflare download/install path, then verifies
  the executable after installation before it starts the temporary tunnel.
- Added a checked generator for
  `LA_STUDIO_SEPARATION_SPLEETER_2STEMS_GPU.ipynb`. The notebook locks its
  worker files to immutable source commit `2502485` and checks both SHA-256
  values. The fixed launcher hash is
  `437bc329e27d18cde12c095e766805550a0493bfc951620049432f0221241a72`.
- Added regression checks for the missing-binary path and for generated
  notebook drift. Direct Colab remains independent of API Gateway and does
  not start a Local model.

## Evidence

- Generated exact-model Colab notebooks: **32/32 PASS**.
- `TestColabSeparationRunner`: **7/7 PASS**.
- Full CTest: **39/39 PASS** in **133.83 seconds**, including offscreen QML
  smoke.
- Graphify knowledge graph was refreshed after the source change.

## Manual live step still required

This is not a claim that the user's existing Colab runtime was changed. Open
the current Spleeter notebook from `main`, create a fresh Colab runtime, Run
all cells, then copy the newly printed URL and token into Dubbing Colab setup
and use **Check Colab**. The expected first-run behavior is to install
`cloudflared` instead of crashing when it is absent. No new EXE was packaged.
