# tam-edit

tam-edit is a personal fork of [micro](https://github.com/micro-editor/micro),
the terminal text editor, kept by Fred at
[greenermoose/tam-edit](https://github.com/greenermoose/tam-edit). It started
as a rename of micro and is being rewritten in C11 so that the editor links
only [libtam](https://github.com/greenermoose/libtam) and the C library.

The fork point is upstream micro at commit `ab37da30`, 94 commits past the
`v2.0.15` release, so master is micro 2.0.15 lineage. Upstream is tracked
through an `upstream` remote, and the Go module path stays
`github.com/micro-editor/micro/v2` so upstream merges remain clean.

Report tam-edit bugs at
[greenermoose/tam-edit/issues](https://github.com/greenermoose/tam-edit/issues),
not upstream. The [upstream reference](#upstream) below maps micro's source
and documentation to the features being ported.

## Branches and status

| Branch | Tree | Build | Status |
| --- | --- | --- | --- |
| `master` | Go, micro's packages with the fork's changes | `make build` (Go 1.19 or later) | Behaviour reference, still usable |
| `c11` | `src/`, a native C11 editor | `make` (libtam 0.7.0 or later) | 0.1.0: daily-usable editor |

New work happens on `c11`. [ROADMAP.md](ROADMAP.md) holds the phase plan and
the milestone definitions. The Go tree stays on `master`, and on `c11` until
the C version reaches the 1.0.0 parity gate.

Version 0.1.0 (Milestone 1) is a daily-usable editor. `make` builds
`build/tam-edit`, which links only libc and libtam 0.7.0. It features a
double-buffered cell-grid renderer with damage tracking, micro's grouped
undo and redo, visual selections, word motions and deletions, default micro
keybindings, interactive command/search/goto prompts, clipboard integration
(OSC 52 and system tools), and mouse support.

## Versioning

tam-edit follows semantic versioning, and while it is below 1.0.0 the minor
number names a milestone: 0.1.0 is the first daily-usable editor, and 1.0.0 is
reserved for the parity gate with micro. Until the first milestone the version
stays in 0.0.x, where it is now. `VERSION` in the Makefile is the only source;
`tam-edit --version` prints it next to the libtam version. The milestone table
and the bump rules are in
[ROADMAP.md](ROADMAP.md#milestones-and-versions).

## What the fork changes from micro

- The binary and command name are `tam-edit`.
- In the Go reference, configuration comes from `TAM_EDIT_CONFIG_HOME`, then
  `$XDG_CONFIG_HOME/tam-edit`, then `~/.config/tam-edit`. It never falls back
  to an existing micro configuration directory.
- The Go reference accepts `TAM_EDIT_TRUECOLOR` alongside `MICRO_TRUECOLOR`, which still
  works because it describes the terminal rather than the configuration.
- Go-reference Lua plugins may import `tam-edit/*` as well as `micro/*`.
- The reference toolchain is pinned in `mise.toml`; Go is optional for C11 work.
- The C11 editor on `c11` is C only and leaves out Go, `tcell`, Lua plugins,
  the plugin manager and the embedded terminal, which are v1 non-goals in
  [ROADMAP.md](ROADMAP.md).

## Building

### The C11 editor (`c11`)

Requires a C11 compiler, `make`, `pkg-config` and libtam 0.7.0 or later.

```sh
make                 # build/tam-edit
make test            # unit tests
make check-deps      # fail if anything other than libc is linked
make install PREFIX=$HOME/.local/dev
make uninstall
```

Run it as `tam-edit FILE`. `PREFIX` defaults to `$HOME/.local/dev` and the
install targets respect `DESTDIR`. If libtam is outside pkg-config's search
path, add its `pkgconfig` directory to `PKG_CONFIG_PATH`.

### The Go editor (`master`)

Requires Go 1.19 or later.

```sh
make build           # ./tam-edit
make test
```

The Go tree is also present on `c11`, but the C11 Makefile does not build it.
To check the reference here, use `go build ./...` and `go test ./...`.
The bundled Lua plugins have been pruned from `c11`; use `master` when
comparing behavior that depends on them. The Go source, existing tests,
syntax definitions, colorschemes and help pages remain available beside the C
code. Upstream release automation, packaging and artwork are kept in Git
history rather than this working tree.

## License

The C11 editor uses GPLv3, as recorded in [ROADMAP.md](ROADMAP.md), with the
full text in [LICENSE](LICENSE). [libtam](https://github.com/greenermoose/libtam)
also uses GPLv3. micro's MIT permission notice is kept in [LICENSE-MIT](LICENSE-MIT) and
covers the Go tree and the runtime data that come from micro;
[LICENSE-THIRD-PARTY](LICENSE-THIRD-PARTY) carries the notices for the
third-party libraries micro uses.

## Upstream

micro's own documentation — every option, command, keybinding and plugin — is
at https://micro-editor.github.io and in
[micro-editor/micro](https://github.com/micro-editor/micro). The Go tree on
`master` carries the same source, so `git show master:runtime/help/options.md`
answers most questions without leaving the clone.

The parts of the Go tree worth reading before porting a feature:

| Concern | Look at |
| --- | --- |
| Buffer, cursors, undo, save, search | `internal/buffer/` — `buffer.go`, `cursor.go`, `eventhandler.go`, `save.go`, `search.go` |
| Commands, keybindings, panes, tabs | `internal/action/` — `actions.go`, `bindings.go`, `command.go`, `pane.go` |
| Settings, colorschemes, runtime files | `internal/config/` — `settings.go`, `colorscheme.go`, `rtfiles.go` |
| Drawing, statusline, soft wrap | `internal/display/` |
| Highlighting state machine | `pkg/highlight/` — `highlighter.go`, `parser.go` |
| Lua plugin API (deferred) | `internal/lua/` |
| Syntax definitions and colours | `runtime/syntax/`, `runtime/colorschemes/` |

Bug reports and fixes come from
[micro's issue tracker](https://github.com/micro-editor/micro/issues). `master`
keeps upstream's history, so `git log` there records what was fixed and why.
