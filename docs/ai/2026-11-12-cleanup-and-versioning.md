# Complete repository cleanup and milestone versioning

- **Date**: 2026-11-12
- **Tools**: OpenCode 2.2.5 (`big-pickle`); Codex CLI 0.167.2 (`gpt-6.1-sol`).
- **Transcript**: Retained privately by the author.
- **Authorship**: OpenCode wrote the README, milestone mapping and initial
  pruning changes. Codex inspected the interrupted session and working tree,
  completed cleanup, corrected documentation and build dependencies, verified
  the result and prepared these records. Fred directed both sessions.
- **Starting commit**: `ed41363d9bfb7efeaddf9adfd4ecdf02e78c5a5f`.

## Guiding prompts

To OpenCode:

> Take a look at tam-edit. Is it clear from the README what this cloned and forked repo is?

> Yes

> Do a thorough inspection of tam-edit. Clean and prune anything in it that is not essential for its purpose. We should pare it down so we can achieve milestones in our ROADMAP as quickly as possible without distracting our AI agents with unnecessary cruft and details from the upstream project. All we care about from upstream are the data structures, algorithms, architectural design choices, and bug reports and fixes so we can get our version working reliably.

> Start a new semver for tam-edit. We are currrently at 0.0.1. Decide how milestones in the ROADMAP map to semvers and document it. I.e., is milestone 1 = 0.1.0 or 1.0.0?

> go

To Codex:

> opencode ran out of tokens working on tam-edit. Please look at its session log and finish what it started.

## Result

- Retain the Go source/tests, syntax data, colorschemes, help and buffer-test
  generator. Remove inherited release automation, packaging, artwork and Lua
  plugins; adjust the Go embed list to match. Keep Git history as the full
  upstream reference.
- Restore the Go toolchain pin so reference builds remain usable on `c11`.
- Explain the fork and current limitations; keep version 0.0.1 in the
  Makefile, compile it into the binary and document milestone exits through
  0.1.0–0.4.0 and the 1.0.0 parity gate.
- Apply the roadmap's GPLv3 decision while preserving micro's MIT notice and
  third-party notices. Fix broken local links and distinguish Go-reference
  features from current C11 behavior.
- Rebuild when the Makefile changes; allow absolute test build directories
  and propagate linker flags to the test executable.

## Verification

- `make all test check-deps`: all ten C editor tests pass; dependency inspection
  shows libc plus the platform loader and virtual DSO.
- ASan/UBSan build and all ten editor tests pass with leak detection enabled.
  The sanitizer runtime required execution outside the development sandbox.
  The linked prebuilt libtam archive was not rebuilt with sanitizers here.
- `go build ./...` and `go test ./...` pass with Go 1.27.4.
- A pseudo-terminal smoke check passes UTF-8 insertion, CRLF save, normal
  quit, dirty-quit confirmation and terminal attribute restoration.
- Local README/roadmap links, preserved MIT/GPL texts, unchanged algorithm
  source/data and `git diff --check` pass.

This work completes the cleanup and versioning pass. It does not complete a
roadmap milestone or create a release.
