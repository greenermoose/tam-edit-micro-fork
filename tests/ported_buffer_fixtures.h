/* Auto-generated from internal/buffer/buffer_generated_test.go (VSCode model tests) */
#ifndef PORTED_BUFFER_FIXTURES_H
#define PORTED_BUFFER_FIXTURES_H

#include <stddef.h>

typedef struct {
    size_t start_col, start_line;
    size_t end_col, end_line;
    const char *text;
} ported_op_t;

typedef struct {
    const char *before;
    const ported_op_t *ops;
    size_t op_count;
    const char *after;
} ported_case_t;

static const ported_op_t k_ported_ops_0[] = {
    {1, 0, 1, 0, "b\nr\nfq"},
    {3, 0, 0, 1, "\n"},
};
static const ported_op_t k_ported_ops_1[] = {
    {1, 0, 1, 0, "\no"},
    {3, 1, 3, 1, "zaq\navb"},
    {4, 1, 1, 5, "jlr\nzl\nj"},
};
static const ported_op_t k_ported_ops_2[] = {
    {2, 3, 2, 3, ""},
};
static const ported_op_t k_ported_ops_3[] = {
    {2, 0, 4, 0, "hp"},
    {6, 0, 0, 1, "kcg\n\nmpx"},
    {1, 1, 1, 1, "\naw\n"},
    {1, 1, 1, 1, "vqr\nmo"},
    {1, 3, 2, 4, "xyc"},
};
static const ported_op_t k_ported_ops_4[] = {
    {0, 1, 1, 1, ""},
    {0, 2, 1, 3, ""},
};
static const ported_op_t k_ported_ops_5[] = {
    {0, 1, 0, 1, " "},
    {0, 2, 0, 2, "\n "},
};
static const ported_op_t k_ported_ops_6[] = {
    {0, 0, 0, 0, ""},
};
static const ported_op_t k_ported_ops_7[] = {
    {0, 0, 1, 0, ""},
    {0, 3, 0, 3, ""},
};
static const ported_op_t k_ported_ops_8[] = {
    {0, 0, 0, 0, "foo "},
};
static const ported_op_t k_ported_ops_9[] = {
    {2, 0, 2, 0, " foo"},
};
static const ported_op_t k_ported_ops_10[] = {
    {3, 0, 3, 0, "\n"},
};
static const ported_op_t k_ported_ops_11[] = {
    {2, 0, 2, 0, " new line\nNo longer"},
};
static const ported_op_t k_ported_ops_12[] = {
    {2, 0, 2, 0, " new line\nOne more line in the middle\nNo longer"},
};
static const ported_op_t k_ported_ops_13[] = {
    {2, 0, 2, 0, "\n\n\n\n"},
};
static const ported_op_t k_ported_ops_14[] = {
    {2, 0, 2, 0, "\n\n\n\n"},
    {14, 2, 14, 2, "a\nb"},
};
static const ported_op_t k_ported_ops_15[] = {
    {0, 0, 0, 0, ""},
};
static const ported_op_t k_ported_ops_16[] = {
    {0, 0, 1, 0, ""},
};
static const ported_op_t k_ported_ops_17[] = {
    {0, 0, 2, 0, "a"},
};
static const ported_op_t k_ported_ops_18[] = {
    {0, 0, 13, 0, ""},
};
static const ported_op_t k_ported_ops_19[] = {
    {3, 0, 5, 1, ""},
};
static const ported_op_t k_ported_ops_20[] = {
    {3, 0, 4, 2, ""},
};
static const ported_op_t k_ported_ops_21[] = {
    {0, 0, 1, 4, ""},
};
static const ported_op_t k_ported_ops_22[] = {
    {0, 1, 2, 1, "\t"},
    {0, 2, 4, 2, ""},
};
static const ported_op_t k_ported_ops_23[] = {
    {2, 4, 6, 4, ""},
    {11, 4, 15, 4, ""},
};
static const ported_op_t k_ported_ops_24[] = {
    {1, 0, 1, 0, "\n  "},
    {4, 0, 5, 0, ""},
    {8, 0, 8, 0, "\n"},
};
static const ported_op_t k_ported_ops_25[] = {
    {1, 0, 2, 1, ""},
    {5, 1, 5, 1, " "},
    {8, 1, 0, 2, ""},
};
static const ported_op_t k_ported_ops_26[] = {
    {1, 0, 0, 1, "\n\t"},
    {10, 1, 0, 3, "\n\t"},
};
static const ported_op_t k_ported_ops_27[] = {
    {4, 0, 0, 2, " text\nsome more text\nsome more text"},
    {1, 2, 0, 3, "o more lines\nasd\nasd\nasd"},
    {0, 4, 5, 4, "zzzzzzzz"},
    {10, 4, 15, 5, "1\n2\n3\n4"},
};
static const ported_op_t k_ported_ops_28[] = {
    {0, 0, 1, 0, ""},
    {2, 0, 9, 0, "\n  "},
    {15, 0, 13, 1, "\n    "},
    {17, 1, 8, 2, "\n  "},
    {21, 2, 8, 3, ""},
    {9, 3, 9, 3, "\n  "},
    {27, 3, 27, 3, "\n    "},
    {31, 3, 31, 3, "\n  "},
    {32, 3, 33, 3, "\n"},
};
static const ported_op_t k_ported_ops_29[] = {
    {0, 0, 3, 0, ""},
    {6, 0, 1, 1, ""},
    {2, 1, 2, 1, "\n"},
};
static const ported_op_t k_ported_ops_30[] = {
    {0, 0, 0, 5, "package caddy\n\nimport \"fmt\"\n\nfunc main() {\n\tfmt.Println(\"Hello World! :)\")\n}\n"},
};
static const ported_op_t k_ported_ops_31[] = {
    {9, 0, 0, 1, "\n"},
};
static const ported_op_t k_ported_ops_32[] = {
    {7, 0, 8, 0, "\n"},
    {16, 2, 17, 2, "\n"},
    {17, 2, 17, 2, "    "},
    {4, 3, 4, 3, "    "},
};
static const ported_op_t k_ported_ops_33[] = {
    {0, 0, 0, 0, "a"},
    {0, 0, 0, 0, "b"},
};
static const ported_op_t k_ported_ops_34[] = {
    {0, 0, 0, 0, "b"},
    {0, 0, 2, 0, "ab"},
};
static const ported_op_t k_ported_ops_35[] = {
    {0, 0, 1, 0, "H"},
    {1, 0, 2, 0, "E"},
};
static const ported_op_t k_ported_ops_36[] = {
    {0, 0, 1, 0, ""},
    {1, 0, 2, 0, ""},
};
static const ported_op_t k_ported_ops_37[] = {
    {0, 0, 0, 0, "H"},
    {0, 0, 2, 0, "e"},
};
static const ported_op_t k_ported_ops_38[] = {
    {0, 0, 2, 0, "H"},
    {2, 0, 2, 0, "e"},
};
static const ported_op_t k_ported_ops_39[] = {
    {0, 0, 1, 0, ""},
};
static const ported_op_t k_ported_ops_40[] = {
    {2, 0, 7, 0, ""},
};
static const ported_op_t k_ported_ops_41[] = {
    {0, 0, 5, 0, ""},
};
static const ported_op_t k_ported_ops_42[] = {
    {1, 0, 6, 0, ""},
};
static const ported_op_t k_ported_ops_43[] = {
    {0, 0, 11, 0, ""},
};
static const ported_op_t k_ported_ops_44[] = {
    {5, 0, 5, 2, ""},
};
static const ported_op_t k_ported_ops_45[] = {
    {11, 0, 11, 2, ""},
};
static const ported_op_t k_ported_ops_46[] = {
    {0, 0, 0, 2, ""},
};
static const ported_op_t k_ported_ops_47[] = {
    {11, 0, 0, 2, ""},
};
static const ported_op_t k_ported_ops_48[] = {
    {0, 0, 0, 0, "xx"},
};
static const ported_op_t k_ported_ops_49[] = {
    {1, 0, 1, 0, "xx"},
};
static const ported_op_t k_ported_ops_50[] = {
    {5, 0, 5, 0, "xx"},
};
static const ported_op_t k_ported_ops_51[] = {
    {6, 0, 6, 0, "xx"},
};
static const ported_op_t k_ported_ops_52[] = {
    {11, 0, 11, 0, "xx"},
};
static const ported_op_t k_ported_ops_53[] = {
    {0, 0, 0, 0, "\n"},
};
static const ported_op_t k_ported_ops_54[] = {
    {11, 0, 11, 0, "\n"},
};
static const ported_op_t k_ported_ops_55[] = {
    {6, 0, 6, 0, "\n"},
};
static const ported_op_t k_ported_ops_56[] = {
    {6, 0, 6, 0, "xx\nyy"},
};

static const ported_case_t k_ported_cases[] = {
    {"ioe\n\nyjct\n\n", k_ported_ops_0, 2, "ib\nr\nfqoe\n\nyjct\n\n"},
    {"f\nlittnhskrq\nutxvsizqnk\nlslqz\njxn\ngmm", k_ported_ops_1, 3, "f\no\nlitzaq\navbtjlr\nzl\njmm"},
    {"ofw\nqsxmziuvzw\nrp\nqsnymek\nelth\nwmgzbwudxz\niwsdkndh\nbujlbwb\nasuouxfv\nxuccnb", k_ported_ops_2, 1, "ofw\nqsxmziuvzw\nrp\nqsnymek\nelth\nwmgzbwudxz\niwsdkndh\nbujlbwb\nasuouxfv\nxuccnb"},
    {"fefymj\nqum\nvmiwxxaiqq\ndz\nlnqdgorosf", k_ported_ops_3, 5, "fehpmjkcg\n\nmpxq\naw\nvqr\nmoum\nvmiwxxaiqq\ndxycqdgorosf"},
    {"something\n A\n\n B\nsomething else", k_ported_ops_4, 2, "something\nA\nB\nsomething else"},
    {"something\nA\nB\nsomething else", k_ported_ops_5, 2, "something\n A\n\n B\nsomething else"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_6, 1, "My First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_7, 2, "y First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_8, 1, "foo My First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_9, 1, "My foo First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_10, 1, "My \nFirst Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_11, 1, "My new line\nNo longer First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_12, 1, "My new line\nOne more line in the middle\nNo longer First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_13, 1, "My\n\n\n\n First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_14, 2, "My\n\n\n\n First Line\n\t\tMy Second Line\n    Third Linea\nb\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_15, 1, "My First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_16, 1, "y First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_17, 1, "a First Line\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_18, 1, "\n\t\tMy Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_19, 1, "My Second Line\n    Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_20, 1, "My Third Line\n\n1"},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n1", k_ported_ops_21, 1, ""},
    {"My First Line\n\t\tMy Second Line\n    Third Line\n\n123", k_ported_ops_22, 2, "My First Line\n\tMy Second Line\nThird Line\n\n123"},
    {"\t\tfirst\t    \n\t\tsecond line\n\tthird line\nfourth line\n\t\t<!@#fifth#@!>\t\t", k_ported_ops_23, 2, "\t\tfirst\t    \n\t\tsecond line\n\tthird line\nfourth line\n\t\tfifth\t\t"},
    {"{\"x\" : 1}", k_ported_ops_24, 3, "{\n  \"x\": 1\n}"},
    {"{\n  \"x\": 1\n}", k_ported_ops_25, 3, "{\"x\" : 1}"},
    {"{\n\"a\": true,\n\n\"b\": true\n}", k_ported_ops_26, 2, "{\n\t\"a\": true,\n\t\"b\": true\n}"},
    {"some text\nsome more text\nnow comes an empty line\n\nafter empty line\nand the last line", k_ported_ops_27, 4, "some text\nsome more text\nsome more textno more lines\nasd\nasd\nasd\nzzzzzzzz empt1\n2\n3\n4ne"},
    {" {       \"d\": [\n             null\n        ] /*comment*/\n        ,\"e\": /*comment*/ [null] }", k_ported_ops_28, 9, "{\n  \"d\": [\n    null\n  ] /*comment*/,\n  \"e\": /*comment*/ [\n    null\n  ]\n}"},
    {"   abc\n ,def", k_ported_ops_29, 3, "abc,\ndef"},
    {"package caddy\n\nfunc main() {\n\tfmt.Println(\"Hello World! :)\")\n}\n", k_ported_ops_30, 1, "package caddy\n\nimport \"fmt\"\n\nfunc main() {\n\tfmt.Println(\"Hello World! :)\")\n}\n"},
    {"something\ninteresting", k_ported_ops_31, 1, "something\ninteresting"},
    {"class A {\n    someProperty = false;\n    someMethod() {\n    this.someMethod();\n    }\n}", k_ported_ops_32, 4, "class A\n{\n    someProperty = false;\n    someMethod()\n    {\n        this.someMethod();\n    }\n}"},
    {"hello world", k_ported_ops_33, 2, "abhello world"},
    {"hello world", k_ported_ops_34, 2, "babllo world"},
    {"hello world", k_ported_ops_35, 2, "HEllo world"},
    {"hello world", k_ported_ops_36, 2, "llo world"},
    {"hello world", k_ported_ops_37, 2, "Hello world"},
    {"hello world", k_ported_ops_38, 2, "Hello world"},
    {"hello world", k_ported_ops_39, 1, "ello world"},
    {"helloworld", k_ported_ops_40, 1, "herld"},
    {"hello world", k_ported_ops_41, 1, " world"},
    {"hello world", k_ported_ops_42, 1, "hworld"},
    {"hello world", k_ported_ops_43, 1, ""},
    {"hello world\nhello world\nhello world", k_ported_ops_44, 1, "hello world"},
    {"hello world\nhello world\nhello world", k_ported_ops_45, 1, "hello world"},
    {"hello world\nhello world\nhello world", k_ported_ops_46, 1, "hello world"},
    {"hello world\nhello world\nhello world", k_ported_ops_47, 1, "hello worldhello world"},
    {"hello world", k_ported_ops_48, 1, "xxhello world"},
    {"hello world", k_ported_ops_49, 1, "hxxello world"},
    {"hello world", k_ported_ops_50, 1, "helloxx world"},
    {"hello world", k_ported_ops_51, 1, "hello xxworld"},
    {"hello world", k_ported_ops_52, 1, "hello worldxx"},
    {"hello world", k_ported_ops_53, 1, "\nhello world"},
    {"hello world", k_ported_ops_54, 1, "hello world\n"},
    {"hello world", k_ported_ops_55, 1, "hello \nworld"},
    {"hello world\nhello world", k_ported_ops_56, 1, "hello xx\nyyworld\nhello world"},
};

#define PORTED_CASE_COUNT 57
#endif
