//go:build ignore

package main

import (
	"bytes"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"

	"gopkg.in/yaml.v2"
)

type HeaderYaml struct {
	FileType string `yaml:"filetype"`
	Detect   struct {
		Filename  string `yaml:"filename"`
		Header    string `yaml:"header"`
		Signature string `yaml:"signature"`
	} `yaml:"detect"`
}

type PatSpec struct {
	GroupID uint16
	Pattern string
}

type RegSpec struct {
	GroupID      uint16
	LimitGroupID uint16
	ParentIdx    int32
	StartPat     string
	EndPat       string
	SkipPat      string
	PatStart     uint32
	PatCount     uint32
	SubregStart  uint32
	SubregCount  uint32
	IncStart     uint32
	IncCount     uint32
}

type DefSpec struct {
	FileType     string
	FilenamePat  string
	HeaderPat    string
	SignaturePat string
	PatStart     uint32
	PatCount     uint32
	RegStart     uint32
	RegCount     uint32
	IncStart     uint32
	IncCount     uint32
}

var (
	groups     []string
	groupMap   = map[string]uint16{}
	allPats    []PatSpec
	allRegs    []RegSpec
	allChildIx []uint32
	allIncs    []string
	allDefs    []DefSpec
)

func internGroup(name string) uint16 {
	if id, ok := groupMap[name]; ok {
		return id
	}
	id := uint16(len(groups))
	groups = append(groups, name)
	groupMap[name] = id
	return id
}

func cQuote(s string) string {
	var b strings.Builder
	b.WriteByte('"')
	for i := 0; i < len(s); i++ {
		c := s[i]
		switch c {
		case '\\':
			b.WriteString("\\\\")
		case '"':
			b.WriteString("\\\"")
		case '?':
			if i > 0 && s[i-1] == '?' {
				b.WriteString("\\?")
			} else {
				b.WriteByte('?')
			}
		case '\n':
			b.WriteString("\\n")
		case '\r':
			b.WriteString("\\r")
		case '\t':
			b.WriteString("\\t")
		default:
			if c < 0x20 || c == 0x7f {
				fmt.Fprintf(&b, "\\x%02x", c)
			} else {
				b.WriteByte(c)
			}
		}
	}
	b.WriteByte('"')
	return b.String()
}

type strPool struct {
	decls strings.Builder
	count int
}

func (sp *strPool) emitExpr(s string) string {
	if s == "" {
		return "NULL"
	}
	if len(s) <= 3500 {
		return cQuote(s)
	}
	name := fmt.Sprintf("g_long_str_%d", sp.count)
	sp.count++
	fmt.Fprintf(&sp.decls, "static const unsigned char %s[] = {\n", name)
	for i := 0; i < len(s); i++ {
		if i%16 == 0 {
			sp.decls.WriteString("   ")
		}
		fmt.Fprintf(&sp.decls, " 0x%02x,", s[i])
		if i%16 == 15 {
			sp.decls.WriteByte('\n')
		}
	}
	if len(s)%16 != 0 {
		sp.decls.WriteByte('\n')
	}
	sp.decls.WriteString("    0x00\n};\n")
	return "(const char *)" + name
}

func (sp *strPool) emitReqExpr(s string) string {
	if s == "" {
		return "\"\""
	}
	return sp.emitExpr(s)
}

func parseRuleBlock(rawRules []any, parentIdx int32) (patStart, patCount, regStart, regCount, incStart, incCount uint32, err error) {
	var pats []PatSpec
	var childRegs []uint32
	var incs []string

	for _, item := range rawRules {
		m, ok := item.(map[any]any)
		if !ok {
			return 0, 0, 0, 0, 0, 0, fmt.Errorf("rule entry is %T, expected map", item)
		}
		for k, v := range m {
			key, ok := k.(string)
			if !ok {
				return 0, 0, 0, 0, 0, 0, fmt.Errorf("rule key is %T, expected string", k)
			}
			switch obj := v.(type) {
			case string:
				if obj == "" {
					return 0, 0, 0, 0, 0, 0, fmt.Errorf("empty rule for %s", key)
				}
				if key == "include" {
					incs = append(incs, obj)
				} else {
					if _, err := regexp.Compile(obj); err != nil {
						return 0, 0, 0, 0, 0, 0, fmt.Errorf("invalid regex %q: %w", obj, err)
					}
					pats = append(pats, PatSpec{
						GroupID: internGroup(key),
						Pattern: obj,
					})
				}
			case map[any]any:
				regIdx, err := parseRegionBlock(key, obj, parentIdx)
				if err != nil {
					return 0, 0, 0, 0, 0, 0, err
				}
				childRegs = append(childRegs, regIdx)
			default:
				return 0, 0, 0, 0, 0, 0, fmt.Errorf("bad rule value %T for %s", v, key)
			}
		}
	}

	patStart = uint32(len(allPats))
	patCount = uint32(len(pats))
	allPats = append(allPats, pats...)

	regStart = uint32(len(allChildIx))
	regCount = uint32(len(childRegs))
	allChildIx = append(allChildIx, childRegs...)

	incStart = uint32(len(allIncs))
	incCount = uint32(len(incs))
	allIncs = append(allIncs, incs...)

	return patStart, patCount, regStart, regCount, incStart, incCount, nil
}

func parseRegionBlock(group string, info map[any]any, parentIdx int32) (uint32, error) {
	regIdx := uint32(len(allRegs))
	allRegs = append(allRegs, RegSpec{})

	gid := internGroup(group)
	limitGID := gid

	startStr, _ := info["start"].(string)
	endStr, _ := info["end"].(string)
	skipStr, _ := info["skip"].(string)
	if startStr == "" || endStr == "" {
		return 0, fmt.Errorf("region %s missing start/end", group)
	}
	if _, err := regexp.Compile(startStr); err != nil {
		return 0, fmt.Errorf("region %s start regex: %w", group, err)
	}
	if _, err := regexp.Compile(endStr); err != nil {
		return 0, fmt.Errorf("region %s end regex: %w", group, err)
	}
	if skipStr != "" {
		if _, err := regexp.Compile(skipStr); err != nil {
			return 0, fmt.Errorf("region %s skip regex: %w", group, err)
		}
	}
	if lg, ok := info["limit-group"].(string); ok && lg != "" {
		limitGID = internGroup(lg)
	}

	var pStart, pCount, rStart, rCount, iStart, iCount uint32
	if rawSub, ok := info["rules"]; ok && rawSub != nil {
		subList, ok := rawSub.([]any)
		if !ok {
			return 0, fmt.Errorf("region %s rules is %T", group, rawSub)
		}
		var err error
		pStart, pCount, rStart, rCount, iStart, iCount, err = parseRuleBlock(subList, int32(regIdx))
		if err != nil {
			return 0, err
		}
	}

	allRegs[regIdx] = RegSpec{
		GroupID:      gid,
		LimitGroupID: limitGID,
		ParentIdx:    parentIdx,
		StartPat:     startStr,
		EndPat:       endStr,
		SkipPat:      skipStr,
		PatStart:     pStart,
		PatCount:     pCount,
		SubregStart:  rStart,
		SubregCount:  rCount,
		IncStart:     iStart,
		IncCount:     iCount,
	}
	return regIdx, nil
}

func generateSyntax() error {
	internGroup("default")

	paths, err := filepath.Glob("runtime/syntax/*.yaml")
	if err != nil {
		return err
	}
	sort.Strings(paths)

	for _, p := range paths {
		raw, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		var hdr HeaderYaml
		if err := yaml.Unmarshal(raw, &hdr); err != nil {
			return fmt.Errorf("%s: %w", p, err)
		}
		if hdr.FileType == "" {
			return fmt.Errorf("%s: missing filetype", p)
		}
		if hdr.Detect.Filename != "" {
			if _, err := regexp.Compile(hdr.Detect.Filename); err != nil {
				return fmt.Errorf("%s filename regex: %w", p, err)
			}
		}
		if hdr.Detect.Header != "" {
			if _, err := regexp.Compile(hdr.Detect.Header); err != nil {
				return fmt.Errorf("%s header regex: %w", p, err)
			}
		}
		if hdr.Detect.Signature != "" {
			if _, err := regexp.Compile(hdr.Detect.Signature); err != nil {
				return fmt.Errorf("%s signature regex: %w", p, err)
			}
		}

		var doc map[any]any
		if err := yaml.Unmarshal(raw, &doc); err != nil {
			return fmt.Errorf("%s: %w", p, err)
		}

		var pStart, pCount, rStart, rCount, iStart, iCount uint32
		if rawRules, ok := doc["rules"]; ok && rawRules != nil {
			ruleList, ok := rawRules.([]any)
			if !ok {
				return fmt.Errorf("%s: rules is %T", p, rawRules)
			}
			pStart, pCount, rStart, rCount, iStart, iCount, err = parseRuleBlock(ruleList, -1)
			if err != nil {
				return fmt.Errorf("%s: %w", p, err)
			}
		}

		allDefs = append(allDefs, DefSpec{
			FileType:     hdr.FileType,
			FilenamePat:  hdr.Detect.Filename,
			HeaderPat:    hdr.Detect.Header,
			SignaturePat: hdr.Detect.Signature,
			PatStart:     pStart,
			PatCount:     pCount,
			RegStart:     rStart,
			RegCount:     rCount,
			IncStart:     iStart,
			IncCount:     iCount,
		})
	}

	// Write data/syntax_data.h
	hdrContent := `/* Generated by tools/gen_runtime_data.go — do not edit manually. */
#ifndef TAM_EDIT_SYNTAX_DATA_H
#define TAM_EDIT_SYNTAX_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t    group_id;
    const char *pattern;
} syntax_pat_spec_t;

typedef struct {
    uint16_t    group_id;
    uint16_t    limit_group_id;
    int32_t     parent_idx;
    const char *start_pat;
    const char *end_pat;
    const char *skip_pat;
    uint32_t    pat_start;
    uint32_t    pat_count;
    uint32_t    subreg_start;
    uint32_t    subreg_count;
    uint32_t    inc_start;
    uint32_t    inc_count;
} syntax_reg_spec_t;

typedef struct {
    const char *filetype;
    const char *filename_pat;
    const char *header_pat;
    const char *signature_pat;
    uint32_t    pat_start;
    uint32_t    pat_count;
    uint32_t    reg_start;
    uint32_t    reg_count;
    uint32_t    inc_start;
    uint32_t    inc_count;
} syntax_def_spec_t;

extern const char *const       g_embedded_syntax_groups[];
extern const size_t            g_embedded_syntax_groups_count;
extern const syntax_pat_spec_t g_embedded_syntax_pats[];
extern const size_t            g_embedded_syntax_pats_count;
extern const syntax_reg_spec_t g_embedded_syntax_regs[];
extern const size_t            g_embedded_syntax_regs_count;
extern const uint32_t          g_embedded_syntax_child_regs[];
extern const size_t            g_embedded_syntax_child_regs_count;
extern const char *const       g_embedded_syntax_includes[];
extern const size_t            g_embedded_syntax_includes_count;
extern const syntax_def_spec_t g_embedded_syntax_defs[];
extern const size_t            g_embedded_syntax_defs_count;

#endif
`
	if err := os.WriteFile("data/syntax_data.h", []byte(hdrContent), 0644); err != nil {
		return err
	}

	var sp strPool
	var b strings.Builder

	b.WriteString("const char *const g_embedded_syntax_groups[] = {\n")
	for _, g := range groups {
		fmt.Fprintf(&b, "    %s,\n", sp.emitReqExpr(g))
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_groups_count = %d;\n\n", len(groups))

	b.WriteString("const syntax_pat_spec_t g_embedded_syntax_pats[] = {\n")
	if len(allPats) == 0 {
		b.WriteString("    {0, NULL},\n")
	}
	for _, p := range allPats {
		fmt.Fprintf(&b, "    {%d, %s},\n", p.GroupID, sp.emitReqExpr(p.Pattern))
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_pats_count = %d;\n\n", len(allPats))

	b.WriteString("const syntax_reg_spec_t g_embedded_syntax_regs[] = {\n")
	if len(allRegs) == 0 {
		b.WriteString("    {0, 0, -1, NULL, NULL, NULL, 0, 0, 0, 0, 0, 0},\n")
	}
	for _, r := range allRegs {
		fmt.Fprintf(&b, "    {%d, %d, %d, %s, %s, %s, %d, %d, %d, %d, %d, %d},\n",
			r.GroupID, r.LimitGroupID, r.ParentIdx,
			sp.emitReqExpr(r.StartPat), sp.emitReqExpr(r.EndPat), sp.emitExpr(r.SkipPat),
			r.PatStart, r.PatCount, r.SubregStart, r.SubregCount, r.IncStart, r.IncCount)
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_regs_count = %d;\n\n", len(allRegs))

	b.WriteString("const uint32_t g_embedded_syntax_child_regs[] = {\n")
	if len(allChildIx) == 0 {
		b.WriteString("    0,\n")
	}
	for i, ix := range allChildIx {
		if i%16 == 0 {
			b.WriteString("   ")
		}
		fmt.Fprintf(&b, " %d,", ix)
		if i%16 == 15 || i+1 == len(allChildIx) {
			b.WriteByte('\n')
		}
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_child_regs_count = %d;\n\n", len(allChildIx))

	b.WriteString("const char *const g_embedded_syntax_includes[] = {\n")
	if len(allIncs) == 0 {
		b.WriteString("    NULL,\n")
	}
	for _, inc := range allIncs {
		fmt.Fprintf(&b, "    %s,\n", sp.emitReqExpr(inc))
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_includes_count = %d;\n\n", len(allIncs))

	b.WriteString("const syntax_def_spec_t g_embedded_syntax_defs[] = {\n")
	for _, d := range allDefs {
		fmt.Fprintf(&b, "    {%s, %s, %s, %s, %d, %d, %d, %d, %d, %d},\n",
			sp.emitReqExpr(d.FileType), sp.emitExpr(d.FilenamePat), sp.emitExpr(d.HeaderPat), sp.emitExpr(d.SignaturePat),
			d.PatStart, d.PatCount, d.RegStart, d.RegCount, d.IncStart, d.IncCount)
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_syntax_defs_count = %d;\n", len(allDefs))

	var final bytes.Buffer
	final.WriteString("/* Generated by tools/gen_runtime_data.go — do not edit manually. */\n")
	final.WriteString("#include \"syntax_data.h\"\n\n")
	final.WriteString(sp.decls.String())
	if sp.count > 0 {
		final.WriteByte('\n')
	}
	final.WriteString(b.String())

	return os.WriteFile("data/syntax_data.c", final.Bytes(), 0644)
}

func generateColorschemes() error {
	paths, err := filepath.Glob("runtime/colorschemes/*.micro")
	if err != nil {
		return err
	}
	sort.Strings(paths)

	hdr := `/* Generated by tools/gen_runtime_data.go — do not edit manually. */
#ifndef TAM_EDIT_COLORSCHEMES_DATA_H
#define TAM_EDIT_COLORSCHEMES_DATA_H

#include <stddef.h>

typedef struct {
    const char *name;
    const char *data;
} embedded_colorscheme_t;

extern const embedded_colorscheme_t g_embedded_colorschemes[];
extern const size_t                 g_embedded_colorschemes_count;

#endif
`
	if err := os.WriteFile("data/colorschemes_data.h", []byte(hdr), 0644); err != nil {
		return err
	}

	var sp strPool
	var b strings.Builder
	b.WriteString("const embedded_colorscheme_t g_embedded_colorschemes[] = {\n")

	for _, p := range paths {
		raw, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		name := strings.TrimSuffix(filepath.Base(p), ".micro")
		fmt.Fprintf(&b, "    {%s, %s},\n", sp.emitReqExpr(name), sp.emitReqExpr(string(raw)))
	}
	b.WriteString("};\n")
	fmt.Fprintf(&b, "const size_t g_embedded_colorschemes_count = %d;\n", len(paths))

	var final bytes.Buffer
	final.WriteString("/* Generated by tools/gen_runtime_data.go — do not edit manually. */\n")
	final.WriteString("#include \"colorschemes_data.h\"\n\n")
	final.WriteString(sp.decls.String())
	if sp.count > 0 {
		final.WriteByte('\n')
	}
	final.WriteString(b.String())

	return os.WriteFile("data/colorschemes_data.c", final.Bytes(), 0644)
}

func main() {
	if err := os.MkdirAll("data", 0755); err != nil {
		log.Fatal(err)
	}
	if err := generateSyntax(); err != nil {
		log.Fatal(err)
	}
	if err := generateColorschemes(); err != nil {
		log.Fatal(err)
	}
	fmt.Printf("Generated data/: %d defs, %d groups, %d patterns, %d regions, %d includes\n",
		len(allDefs), len(groups), len(allPats), len(allRegs), len(allIncs))
}
