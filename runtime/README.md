# Runtime data

`syntax/` holds micro's YAML syntax definitions and `colorschemes/` its
`color-link` files, both in micro's format. The C11 editor will read them
through generated C tables (ROADMAP.md, Phase 5), so they are kept as data, not
as Go code.

`help/` is micro's documentation, kept as a behaviour reference until tam-edit
has help of its own.

The Go reference on this branch embeds these directories without the bundled
Lua plugins. `master` retains the complete Go editor and plugins. The C11 build
does not read this data yet.
