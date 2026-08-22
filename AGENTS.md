## User delivery contract (highest priority)

The owner expects a working desktop product, not a list of missing pieces,
unverified claims, or repeated requests for the same information. Apply the
following rules to every LA Studio task.

### Act on missing or broken work

- When a required file, endpoint, notebook, UI control, model option, or test
  does not exist, create it if it is within the requested scope. Do not stop at
  reporting that it is missing.
- When a real error is supplied, inspect the relevant log and trace the actual
  UI → controller → service/worker code path before changing anything. Rebuilds
  and retries are not fixes.
- Do not hide an error with a mock, fake readiness, invented percentage,
  silent fallback, disabled action, or a second unrelated backend. Surface an
  actionable error and fix the cause where possible.
- Treat a user report as a regression candidate. Add a focused automated test
  when practical, then validate the exact affected boundary. Do not declare it
  fixed merely because compilation, a mock, or an unrelated test passes.

### Respect scope, choices, and independent routes

- The newest direct user request is authoritative. Do not revive stale
  `AI_AGENT_REQUEST.md` instructions or earlier conversations unless the user
  explicitly asks for them.
- API Gateway, Direct Colab GPU, user-uploaded output, and local CPU are
  separate routes. Never silently switch among them. Every active route must be
  visible in the UI and must execute only the selected route.
- A unified Colab coordinator is optional. Per-model Colab setup remains valid
  and independent. A coordinator must exist as an actual notebook/worker before
  UI can advertise it; it must not return synthetic `ready` responses.
- STT and Subtitle OCR are independently configurable and independently
  runnable. Reconciliation/alignment is a later explicit operation that uses
  completed outputs; it must not block either source from starting.
- If Colab produces an output, the user may either wait for the normal worker
  transfer or upload the documented exact output themselves. Uploading and
  confirming valid output must cancel only that pending transfer and advance
  that task; it must not stop Colab or change other tasks. Restrict accepted
  names/extensions and show the exact Colab save folder.
- Preserve user choice for downloaded media and batch actions. Downloading,
  importing, STT, OCR, translation, isolation, voice generation, subtitle
  export, and final export must not imply unwanted later tasks.

### Desktop UX and visual truth

- Do not lock unrelated tasks because one task is running, unless concurrent
  access would corrupt the same artifact. Explain any necessary lock in the
  UI.
- Keep controls discoverable: a selected model needs an explicit **Apply** and
  a non-destructive **Close**; required Colab/model controls must appear in the
  task that uses them; file uploads must have a visible chooser for every
  supported task output.
- Do not overlap panels. Dubbing uses a stable editor layout: feature/task
  controls on the left, large central video preview, contextual inspector on
  the right, and a full-width timeline below. Panels may be collapsible or
  resizable, but resizing must push neighbouring regions rather than cover
  them. Keep task navigation readable at normal desktop widths.
- Initial project configuration belongs in a create/open-project gate before
  feature work, while optional advanced settings may be revealed on demand.
- Progress is evidence, not decoration. Show real stage/substage progress only
  when backed by worker events or measurable transfers. Otherwise show a clear
  indeterminate state and elapsed time; never freeze at arbitrary values such
  as 3%, 5%, or 90%.

### Testing, packaging, communication, and machine control

- Run the smallest real reproduction first, then targeted regression tests,
  then broader tests appropriate to the changed batch. State separately what
  was compiled, unit-tested, controller-tested, launched manually, tested
  against a live Colab worker, or not tested. Never call offscreen/mocked tests
  full real acceptance.
- Do not package an EXE until the requested fixes are implemented and relevant
  tests are green. Do not package simply to give the user another build to
  discover known errors. Package only when requested or when the active task
  explicitly reaches its delivery gate.
- Do not open, drive, capture, close, or otherwise control the owner's GUI,
  browser, applications, or processes unless the current request explicitly
  authorizes that exact action. Command-line build and test work is allowed
  when requested.
- Do not end a substantial requested batch after a token/minute-sized partial
  action. Continue through all safe, in-scope work, then write the requested
  concise Markdown report with completed work, evidence, remaining blockers,
  and exact artifact paths.
- Work directly on `main` unless the user asks otherwise. Commit only files
  created/changed for the active task; preserve unrelated dirty files. Push the
  valid commit to `origin/main` when the user expects source delivery.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).

## LA Studio delivery

For any LA Studio implementation, investigation, testing, packaging or handoff
work, read and follow `.codex/skills/la-studio-delivery/SKILL.md` before taking
task actions. It supplements the Graphify rules above; use both when the task
needs source investigation.
