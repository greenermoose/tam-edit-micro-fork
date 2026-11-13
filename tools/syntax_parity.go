//go:build ignore

package main

import (
	"bytes"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"unicode/utf8"

	"github.com/micro-editor/micro/v2/pkg/highlight"
)

type parsedFile struct {
	ft     string
	header *highlight.Header
	file   *highlight.File
}

func loadAllDefs(syntaxDir string) (map[string]*highlight.Def, []*highlight.File) {
	entries, err := os.ReadDir(syntaxDir)
	if err != nil {
		panic(err)
	}
	var allFiles []*highlight.File
	parsed := make(map[string]*parsedFile)

	for _, ent := range entries {
		if ent.IsDir() || !strings.HasSuffix(ent.Name(), ".yaml") {
			continue
		}
		data, err := os.ReadFile(filepath.Join(syntaxDir, ent.Name()))
		if err != nil {
			panic(err)
		}
		hdr, err := highlight.MakeHeaderYaml(data)
		if err != nil {
			panic(fmt.Errorf("%s: %w", ent.Name(), err))
		}
		f, err := highlight.ParseFile(data)
		if err != nil {
			panic(fmt.Errorf("%s: %w", ent.Name(), err))
		}
		allFiles = append(allFiles, f)
		parsed[f.FileType] = &parsedFile{ft: f.FileType, header: hdr, file: f}
	}

	defs := make(map[string]*highlight.Def)
	for ft, pf := range parsed {
		d, err := highlight.ParseDef(pf.file, pf.header)
		if err != nil {
			panic(fmt.Errorf("ParseDef %s: %w", ft, err))
		}
		highlight.ResolveIncludes(d, allFiles)
		defs[ft] = d
	}
	return defs, allFiles
}

func goLineGroups(lm highlight.LineMatch, runeCount int) []string {
	out := make([]string, runeCount)
	cur := "default"
	for r := 0; r < runeCount; r++ {
		if g, ok := lm[r]; ok {
			if g == 0 {
				cur = "default"
			} else {
				cur = g.String()
			}
		}
		out[r] = cur
	}
	return out
}

func parseTamDump(raw string, lineRunes []int) (string, [][]string, error) {
	lines := strings.Split(strings.TrimSuffix(raw, "\n"), "\n")
	if len(lines) == 0 || !strings.HasPrefix(lines[0], "filetype:") {
		return "", nil, fmt.Errorf("missing filetype line in dump")
	}
	ft := strings.TrimPrefix(lines[0], "filetype:")
	out := make([][]string, len(lineRunes))
	for lIdx := 0; lIdx < len(lineRunes); lIdx++ {
		out[lIdx] = make([]string, lineRunes[lIdx])
		for r := range out[lIdx] {
			out[lIdx][r] = "default"
		}
		if 1+lIdx >= len(lines) {
			return ft, nil, fmt.Errorf("dump has too few lines (%d vs %d)", len(lines)-1, len(lineRunes))
		}
		line := lines[1+lIdx]
		colon := strings.IndexByte(line, ':')
		if colon < 0 {
			return ft, nil, fmt.Errorf("bad line %q", line)
		}
		rest := strings.TrimSpace(line[colon+1:])
		if rest == "" {
			continue
		}
		for _, tok := range strings.Fields(rest) {
			eq := strings.IndexByte(tok, '=')
			dash := strings.IndexByte(tok, '-')
			if eq < 0 || dash < 0 || dash > eq {
				return ft, nil, fmt.Errorf("bad token %q", tok)
			}
			s, err1 := strconv.Atoi(tok[:dash])
			e, err2 := strconv.Atoi(tok[dash+1 : eq])
			grp := tok[eq+1:]
			if err1 != nil || err2 != nil {
				return ft, nil, fmt.Errorf("bad span %q", tok)
			}
			for r := s; r < e && r < len(out[lIdx]); r++ {
				out[lIdx][r] = grp
			}
		}
	}
	return ft, out, nil
}

type sampleCase struct {
	name    string
	ft      string
	path    string
	content string
}

func main() {
	defs, _ := loadAllDefs("runtime/syntax")

	var cases []sampleCase
	realFiles := []struct {
		ft   string
		path string
	}{
		{"c", "src/syntax.c"},
		{"go", "tools/gen_runtime_data.go"},
		{"yaml", "runtime/syntax/c.yaml"},
		{"markdown", "README.md"},
	}
	for _, rf := range realFiles {
		b, err := os.ReadFile(rf.path)
		if err != nil {
			panic(err)
		}
		cases = append(cases, sampleCase{name: rf.path, ft: rf.ft, path: rf.path, content: string(b)})
	}

	inline := []sampleCase{
		{
			name:    "python.py",
			ft:      "python",
			content: "#!/usr/bin/env python3\ndef greet(name: str = \"world\") -> None:\n    for i in range(3):\n        if name != \"\":\n            print(f\"hello {name} {i}\")\n",
		},
		{
			name:    "rust.rs",
			ft:      "rust",
			content: "pub fn add(a: i32, b: i32) -> i32 {\n    /* block\n       comment */\n    let s = r#\"raw\"#;\n    if a > 0 { return a + b; } else { 0 }\n}\n",
		},
		{
			name:    "script.sh",
			ft:      "shell",
			content: "#!/bin/bash\nFOO=\"bar \\\"baz\\\"\"\nif [ -n \"$FOO\" ]; then\n  echo $FOO # done\nfi\n",
		},
		{
			name:    "page.html",
			ft:      "html",
			content: "<!DOCTYPE html>\n<div class=\"a\"><!-- TODO --></div>\n<script>let s = \"hi\";</script>\n",
		},
		{
			name:    "tmpl.j2",
			ft:      "jinja2",
			content: "<div class=\"a\"><script>let s = \"hi\";</script> <!-- TODO --> {{ user }}</div>\n{% if ok %}hello{% endif %}\n",
		},
		{
			name:    "Main.hs",
			ft:      "haskell",
			content: "module Main where\n-- operator Sm test\nf :: Int -> Int\nf x = x + 1\n",
		},
		{
			name:    "Build.groovy",
			ft:      "groovy",
			content: "def x = 'hello'\ndef y = \"world ${x}\"\nprintln x + y\n",
		},
		{
			name:    "test.ps1",
			ft:      "powershell",
			content: "$items = @(1, 2, 3)\nforeach ($i in $items) {\n    Write-Output \"Item: $i\"\n}\n",
		},
		{
			name:    "data.json",
			ft:      "json",
			content: "{\n  \"name\": \"tam-edit\",\n  \"version\": 3,\n  \"ok\": true,\n  \"items\": [null, -1.5e2]\n}\n",
		},
		{
			name:    "query.sql",
			ft:      "sql",
			content: "-- fetch users\nSELECT id, name FROM users WHERE active = 1 ORDER BY id DESC;\n",
		},
		{
			name:    "utf8.c",
			ft:      "c",
			content: "/* naïve café 🚀 */\nconst char *s = \"héllo /* still string */ \\\"world\\\"\"; // end\nint x = 0xDEAD + 42;\n",
		},
	}
	cases = append(cases, inline...)

	tmpDir, err := os.MkdirTemp("", "tam-parity-")
	if err != nil {
		panic(err)
	}
	defer os.RemoveAll(tmpDir)

	mismatches := 0
	totalRunes := 0

	for _, tc := range cases {
		p := tc.path
		if p == "" {
			p = filepath.Join(tmpDir, tc.name)
			if err := os.WriteFile(p, []byte(tc.content), 0644); err != nil {
				panic(err)
			}
		}

		def := defs[tc.ft]
		if def == nil {
			panic("missing def " + tc.ft)
		}
		hl := highlight.NewHighlighter(def)
		goRes := hl.HighlightString(tc.content)

		cmd := exec.Command("./build/tam-edit", "--dump-syntax", p)
		var outBuf bytes.Buffer
		var errBuf bytes.Buffer
		cmd.Stdout = &outBuf
		cmd.Stderr = &errBuf
		if err := cmd.Run(); err != nil {
			panic(fmt.Errorf("--dump-syntax %s failed: %v\n%s", tc.name, err, errBuf.String()))
		}

		srcLines := strings.Split(tc.content, "\n")
		lineRunes := make([]int, len(srcLines))
		for i, sl := range srcLines {
			lineRunes[i] = utf8.RuneCountInString(sl)
		}

		cFt, cRes, err := parseTamDump(outBuf.String(), lineRunes)
		if err != nil {
			panic(fmt.Errorf("%s parse error: %v", tc.name, err))
		}
		if cFt != tc.ft {
			fmt.Printf("[MISMATCH] %s: filetype C=%q Go=%q\n", tc.name, cFt, tc.ft)
			mismatches++
		}

		caseDiff := 0
		for l := 0; l < len(srcLines) && l < len(goRes); l++ {
			goG := goLineGroups(goRes[l], lineRunes[l])
			cG := cRes[l]
			totalRunes += lineRunes[l]
			for r := 0; r < lineRunes[l]; r++ {
				if goG[r] != cG[r] {
					if caseDiff < 8 {
						fmt.Printf("  [DIFF] %s line %d rune %d: Go=%s C=%s | line=%q\n",
							tc.name, l+1, r, goG[r], cG[r], srcLines[l])
					}
					caseDiff++
					mismatches++
				}
			}
		}
		if caseDiff == 0 {
			fmt.Printf("[PASS] %-28s (%d lines, exact parity)\n", tc.name, len(srcLines))
		} else {
			fmt.Printf("[FAIL] %-28s (%d rune diffs)\n", tc.name, caseDiff)
		}
	}

	fmt.Printf("\nTotal runes compared: %d, mismatches: %d\n", totalRunes, mismatches)
	if mismatches > 0 {
		os.Exit(1)
	}
}
