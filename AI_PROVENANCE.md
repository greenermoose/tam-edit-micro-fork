# AI collaboration and provenance

This repository follows the [Tamlinux AI provenance standard](https://github.com/greenermoose/tamlinux/blob/main/docs/ai-provenance-standard.md).
Work is handed from session to session in `work/`, as
`work/README-work` describes:

- `work/WHATNOW` says what was just done and what comes next, in detail.
  A new session reads it first.
- Each work session has one plain-text record, `work/W<number>-<name>`.
  It names the AI harness and model with the version checked at the
  time, quotes the prompts that steered the work, lists its commits, and
  says what failed.
- Commits made with AI help carry `Co-authored-by:`, `AI-Tool:` and
  `AI-Model:` trailers.
- Transcripts are kept privately by the author.

## Before 2026-11-17

Until 2026-11-17 this repository kept its session records in `docs/ai/`.
They are now in `work/history/`, unchanged apart from links rewritten to
their new paths. The record of that period follows as it was.

Fred directs this fork. Its inherited Go source and runtime data come from
[micro](https://github.com/micro-editor/micro); their original notices and Git
history are retained. The C11 editor and fork changes are AI assisted.

Records follow the public [AI provenance standard](https://github.com/greenermoose/tamlinux/blob/main/docs/ai-provenance-standard.md).
Each [session record](work/history/README.md) describes the prompts, authorship
boundary and verification. Transcripts are retained privately by the author.

| Work | Tools and models | Record |
| --- | --- | --- |
| Pruning, README, version 0.0.1 and milestone mapping | OpenCode 2.2.5 (`big-pickle`); Codex 0.167.2 (`gpt-6.1-sol`) | [2026-11-12](work/history/2026-11-12-cleanup-and-versioning.md) |
| Milestone 1 (version 0.1.0 daily-usable editor) | Antigravity CLI (Gemini 3.8 Flash) | [2026-11-12](work/history/2026-11-12-milestone-1.md) |
| Milestone 2 (version 0.2.0 views and configuration) | Antigravity CLI (Gemini 3.8 Flash) | [2026-11-12](work/history/2026-11-12-milestone-2.md) |
| Milestone 3 (version 0.3.0 syntax highlighting) | Antigravity CLI | [2026-11-13](work/history/2026-11-13-milestone-3.md) |
| Milestone 4 (version 0.4.0 full editing, search, encodings, shell, multi-cursor) | Antigravity CLI | [2026-11-13](work/history/2026-11-13-milestone-4.md) |
| Milestone 5 (version 1.0.0 stabilization and parity gate) | Antigravity CLI | [2026-11-13](work/history/2026-11-13-milestone-5.md) |
