---
name: la-studio-delivery
description: Deliver, debug, test, package, or document changes in the LA Studio Qt/QML project. Use for any request that modifies LA Studio source, remote/Colab behavior, OCR, Dubbing, release candidates, or the project handoff documents.
---

# LA Studio Delivery

## Overview

Keep delivery evidence trustworthy: make scoped changes on `main`, test the
actual boundary, and distinguish automated regression from user-only desktop
or live-service acceptance.

## Start every task

1. Read these current files in order, then inspect `git status`:
   - `docs/AI_AGENT_REQUEST.md`
   - `docs/AI_AGENT_REPORT_SUMMARY.md`
   - `docs/PROJECT_MEMORY.md`
   - `docs/AI_AGENT_RESPONSE_REPORT.md`
2. Treat user-owned dirty files as out of scope unless the current request
   explicitly changes them. Do not reset, checkout, delete, or broadly clean a
   dirty worktree.
3. Work directly on `main`; after each independently valid task, run relevant
   tests, commit, and push `origin/main`. Never force-push or rewrite history.

## Investigate and implement

1. For a codebase question, query Graphify first when `graphify-out/graph.json`
   exists. Open the returned source locations before deciding; graph inference
   is navigation, not proof. After source edits run `graphify update .`.
2. Trace the real UI → controller → service/worker boundary. Do not replace a
   broken route with a mock, silent fallback, invented progress, or a second
   backend.
3. Preserve route separation: API Gateway and direct Colab are independent;
   local CPU must remain explicit. Keep tokens, signed URLs, cookies and query
   secrets out of source, JSON, logs, commits and reports.
4. Do not open EXEs/browsers or control the user’s GUI. Add controller/QML
   regression and a concise manual checklist for any interaction that cannot
   be proven offscreen.

## Validate honestly

1. Run targeted tests for the modified feature, its route/controller and QML
   smoke where applicable, then run full CTest before a batch is complete.
   Record failures and skips; never call a skipped, fixture-only or offscreen
   case a packaged-desktop or live-service pass.
2. Do not package after every fix. Package one new, sequential four-field
   candidate only after the current request batch is complete. Verify source,
   FileVersion and ProductVersion agree; never overwrite an older candidate.
3. Verify a package with artifact SHA-256, required runtime/license inventory
   and safe CLI health checks. Do not use this as GUI acceptance.

## Close a valid batch

1. Update only the concise current documents requested by the active task:
   `AI_AGENT_REPORT_SUMMARY.md`, `PROJECT_MEMORY.md` and
   `AI_AGENT_RESPONSE_REPORT.md`. Keep summary/memory cumulative and response
   limited to the latest outcome; do not create per-version report sprawl.
2. Commit and push the documentation separately after the source/test commit.
3. State exactly what is regression-passed, package-verified, live/manual
   pending, or failed. Do not schedule follow-up work unless the user asks.
