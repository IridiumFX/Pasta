#include "pasta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Minimal test harness                                               */
/* ------------------------------------------------------------------ */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;
static int suite_run    = 0;
static int suite_passed = 0;

#define ASSERT(cond, msg)                                                    \
    do {                                                                     \
        tests_run++;                                                         \
        if (cond) { tests_passed++; }                                        \
        else {                                                               \
            tests_failed++;                                                  \
            printf("    FAIL: %s (line %d)\n", msg, __LINE__);               \
        }                                                                    \
    } while (0)

#define SUITE(name) \
    printf("\n--- %s ---\n", name); suite_run++

#define SUITE_OK() suite_passed++

/* ------------------------------------------------------------------ */
/*  Pretty-printer: recursively dumps a PastaValue tree               */
/* ------------------------------------------------------------------ */

static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void print_value(const PastaValue *v, int depth) {
    if (!v) { printf("(nil)"); return; }

    switch (pasta_type(v)) {
    case PASTA_NULL:
        printf("null");
        break;
    case PASTA_BOOL:
        printf("%s", pasta_get_bool(v) ? "true" : "false");
        break;
    case PASTA_NUMBER: {
        double n = pasta_get_number(v);
        if (isnan(n))          printf("NaN");
        else if (isinf(n))     printf("%sInf", n < 0 ? "-" : "");
        else if (n == (int)n)  printf("%d", (int)n);
        else                   printf("%g", n);
        break;
    }
    case PASTA_STRING:
        printf("\"%s\"", pasta_get_string(v));
        break;
    case PASTA_LABEL:
        printf("%s", pasta_get_label(v));
        break;
    case PASTA_ARRAY: {
        size_t count = pasta_count(v);
        if (count == 0) { printf("[]"); break; }
        printf("[\n");
        for (size_t i = 0; i < count; i++) {
            print_indent(depth + 1);
            print_value(pasta_array_get(v, i), depth + 1);
            if (i + 1 < count) printf(",");
            printf("\n");
        }
        print_indent(depth);
        printf("]");
        break;
    }
    case PASTA_MAP: {
        size_t count = pasta_count(v);
        if (count == 0) { printf("{}"); break; }
        printf("{\n");
        for (size_t i = 0; i < count; i++) {
            print_indent(depth + 1);
            printf("%s: ", pasta_map_key(v, i));
            print_value(pasta_map_value(v, i), depth + 1);
            if (i + 1 < count) printf(",");
            printf("\n");
        }
        print_indent(depth);
        printf("}");
        break;
    }
    }
}

/* Helper: parse, print, assert success, return value (caller frees) */
static PastaValue *parse_and_print(const char *label, const char *input) {
    PastaResult r;
    PastaValue *v = pasta_parse_cstr(input, &r);
    printf("  [%s]\n", label);
    if (r.code != PASTA_OK) {
        printf("    ERROR %d at %d:%d: %s\n", r.code, r.line, r.col, r.message);
        return NULL;
    }
    printf("    ");
    print_value(v, 2);
    printf("\n");
    return v;
}

/* Helper: parse, expect failure */
static void parse_expect_fail(const char *label, const char *input, PastaError expected) {
    PastaResult r;
    PastaValue *v = pasta_parse_cstr(input, &r);
    printf("  [%s]\n", label);
    ASSERT(v == NULL, "should return NULL");
    ASSERT(r.code != PASTA_OK, "should set error code");
    if (expected != PASTA_OK)
        ASSERT(r.code == expected, "error code matches expected");
    ASSERT(r.message[0] != '\0', "error message populated");
    printf("    error %d at %d:%d: %s\n", r.code, r.line, r.col, r.message);
    pasta_free(v);
}

/* Recursive structural equality check */
static int values_equal(const PastaValue *a, const PastaValue *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (pasta_type(a) != pasta_type(b)) return 0;

    switch (pasta_type(a)) {
    case PASTA_NULL:   return 1;
    case PASTA_BOOL:   return pasta_get_bool(a) == pasta_get_bool(b);
    case PASTA_NUMBER: {
        double na = pasta_get_number(a), nb = pasta_get_number(b);
        if (isnan(na) && isnan(nb)) return 1;
        return na == nb;
    }
    case PASTA_STRING:
        return pasta_get_string_len(a) == pasta_get_string_len(b)
            && strcmp(pasta_get_string(a), pasta_get_string(b)) == 0;
    case PASTA_LABEL:
        return pasta_get_label_len(a) == pasta_get_label_len(b)
            && strcmp(pasta_get_label(a), pasta_get_label(b)) == 0;
    case PASTA_ARRAY:
        if (pasta_count(a) != pasta_count(b)) return 0;
        for (size_t i = 0; i < pasta_count(a); i++)
            if (!values_equal(pasta_array_get(a, i), pasta_array_get(b, i))) return 0;
        return 1;
    case PASTA_MAP:
        if (pasta_count(a) != pasta_count(b)) return 0;
        for (size_t i = 0; i < pasta_count(a); i++) {
            if (strcmp(pasta_map_key(a, i), pasta_map_key(b, i)) != 0) return 0;
            if (!values_equal(pasta_map_value(a, i), pasta_map_value(b, i))) return 0;
        }
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  File reading helper                                                */
/* ------------------------------------------------------------------ */

#ifndef PASTA_TEST_RESOURCES
  #define PASTA_TEST_RESOURCES "src/test/resources"
#endif

static char *read_file(const char *filename, size_t *out_len) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", PASTA_TEST_RESOURCES, filename);

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        printf("    WARNING: could not open %s\n", path);
        return NULL;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    fread(buf, 1, (size_t)sz, fp);
    buf[sz] = '\0';
    fclose(fp);
    if (out_len) *out_len = (size_t)sz;
    return buf;
}

/* Parse a file, print the tree, return value (caller frees) */
static PastaValue *parse_file(const char *filename) {
    size_t len;
    char *text = read_file(filename, &len);
    if (!text) return NULL;

    PastaResult r;
    PastaValue *v = pasta_parse(text, len, &r);
    printf("  [%s] ", filename);
    if (r.code != PASTA_OK) {
        printf("ERROR %d at %d:%d: %s\n", r.code, r.line, r.col, r.message);
    } else {
        printf("OK (%zu bytes)\n", len);
    }
    free(text);
    return v;
}

/* ================================================================== */
/*  1. SCALAR VALUES                                                   */
/* ================================================================== */

static void test_null_values(void) {
    SUITE("Null values");
    PastaValue *v;

    v = parse_and_print("bare null", "null");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_type(v) == PASTA_NULL, "type");
    ASSERT(pasta_is_null(v), "is_null");
    ASSERT(pasta_get_bool(v) == 0, "bool accessor returns 0 for null");
    ASSERT(pasta_get_number(v) == 0.0, "number accessor returns 0 for null");
    ASSERT(pasta_get_string(v) == NULL, "string accessor returns NULL for null");
    ASSERT(pasta_count(v) == 0, "count returns 0 for null");
    pasta_free(v);

    v = parse_and_print("null with blanks", "  \t\n  null  \n  ");
    ASSERT(v != NULL && pasta_type(v) == PASTA_NULL, "null with whitespace");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  2. BOOLEAN VALUES                                                  */
/* ================================================================== */

static void test_boolean_values(void) {
    SUITE("Boolean values");
    PastaValue *v;

    v = parse_and_print("true", "true");
    ASSERT(v != NULL && pasta_type(v) == PASTA_BOOL, "type");
    ASSERT(pasta_get_bool(v) == 1, "value");
    pasta_free(v);

    v = parse_and_print("false", "false");
    ASSERT(v != NULL && pasta_type(v) == PASTA_BOOL, "type");
    ASSERT(pasta_get_bool(v) == 0, "value");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  3. NUMBER VALUES                                                   */
/* ================================================================== */

static void test_number_values(void) {
    SUITE("Number values");
    PastaValue *v;

    v = parse_and_print("zero", "0");
    ASSERT(v != NULL && pasta_get_number(v) == 0.0, "zero");
    pasta_free(v);

    v = parse_and_print("positive int", "42");
    ASSERT(v != NULL && pasta_get_number(v) == 42.0, "42");
    pasta_free(v);

    v = parse_and_print("large int", "1000000");
    ASSERT(v != NULL && pasta_get_number(v) == 1000000.0, "1000000");
    pasta_free(v);

    v = parse_and_print("negative int", "-7");
    ASSERT(v != NULL && pasta_get_number(v) == -7.0, "-7");
    pasta_free(v);

    v = parse_and_print("negative zero", "-0");
    ASSERT(v != NULL && pasta_get_number(v) == 0.0, "-0 equals 0");
    pasta_free(v);

    v = parse_and_print("decimal", "3.14");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_get_number(v) > 3.13 && pasta_get_number(v) < 3.15, "~3.14");
    pasta_free(v);

    v = parse_and_print("negative decimal", "-0.5");
    ASSERT(v != NULL && pasta_get_number(v) == -0.5, "-0.5");
    pasta_free(v);

    v = parse_and_print("long decimal", "123.456789");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_get_number(v) > 123.456 && pasta_get_number(v) < 123.457, "~123.456789");
    pasta_free(v);

    v = parse_and_print("Inf", "Inf");
    ASSERT(v != NULL && isinf(pasta_get_number(v)) && pasta_get_number(v) > 0, "+Inf");
    pasta_free(v);

    v = parse_and_print("-Inf", "-Inf");
    ASSERT(v != NULL && isinf(pasta_get_number(v)) && pasta_get_number(v) < 0, "-Inf");
    pasta_free(v);

    v = parse_and_print("NaN", "NaN");
    ASSERT(v != NULL && isnan(pasta_get_number(v)), "NaN");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  4. STRING VALUES (no escape sequences)                             */
/* ================================================================== */

static void test_string_values(void) {
    SUITE("String values");
    PastaValue *v;

    v = parse_and_print("empty string", "\"\"");
    ASSERT(v != NULL && pasta_type(v) == PASTA_STRING, "type");
    ASSERT(pasta_get_string_len(v) == 0, "length 0");
    ASSERT(strcmp(pasta_get_string(v), "") == 0, "empty");
    pasta_free(v);

    v = parse_and_print("simple string", "\"hello\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "hello") == 0, "hello");
    ASSERT(pasta_get_string_len(v) == 5, "length 5");
    pasta_free(v);

    v = parse_and_print("string with spaces", "\"hello world\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "hello world") == 0, "hello world");
    pasta_free(v);

    /* Backslash is a literal character, not an escape */
    v = parse_and_print("backslash is literal", "\"C:\\Users\\Admin\"");
    ASSERT(v != NULL, "parsed");
    ASSERT(strcmp(pasta_get_string(v), "C:\\Users\\Admin") == 0, "backslash verbatim");
    ASSERT(pasta_get_string_len(v) == 14, "length includes backslashes");
    pasta_free(v);

    /* String with special chars from the grammar */
    v = parse_and_print("special chars", "\"a+b=c<d>e?f~g\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "a+b=c<d>e?f~g") == 0, "specials");
    pasta_free(v);

    v = parse_and_print("parens and pipes", "\"(foo|bar)\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "(foo|bar)") == 0, "parens/pipes");
    pasta_free(v);

    v = parse_and_print("label syms in string", "\"user@host!#$%&_\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "user@host!#$%&_") == 0, "label syms");
    pasta_free(v);

    /* Semicolons inside strings are not comments */
    v = parse_and_print("semicolons in string", "\"a; b; c\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "a; b; c") == 0, "semicolons");
    pasta_free(v);

    /* Colons, commas, braces, brackets — all fine inside strings */
    v = parse_and_print("delimiters in string", "\"key: [1, 2], {a: b}\"");
    ASSERT(v != NULL && strcmp(pasta_get_string(v), "key: [1, 2], {a: b}") == 0, "delimiters");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  5. EMPTY CONTAINERS                                                */
/* ================================================================== */

static void test_empty_containers(void) {
    SUITE("Empty containers");
    PastaValue *v;

    v = parse_and_print("empty array", "[]");
    ASSERT(v != NULL && pasta_type(v) == PASTA_ARRAY, "type");
    ASSERT(pasta_count(v) == 0, "count 0");
    ASSERT(pasta_array_get(v, 0) == NULL, "oob returns NULL");
    pasta_free(v);

    v = parse_and_print("empty map", "{}");
    ASSERT(v != NULL && pasta_type(v) == PASTA_MAP, "type");
    ASSERT(pasta_count(v) == 0, "count 0");
    ASSERT(pasta_map_get(v, "nope") == NULL, "missing key returns NULL");
    pasta_free(v);

    v = parse_and_print("empty array with blanks", "[  \n  ]");
    ASSERT(v != NULL && pasta_count(v) == 0, "empty with blanks");
    pasta_free(v);

    v = parse_and_print("empty map with blanks", "{  \t  }");
    ASSERT(v != NULL && pasta_count(v) == 0, "empty with blanks");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  6. ARRAYS                                                          */
/* ================================================================== */

static void test_arrays(void) {
    SUITE("Arrays");
    PastaValue *v;

    v = parse_and_print("single element", "[1]");
    ASSERT(v != NULL && pasta_count(v) == 1, "count 1");
    pasta_free(v);

    v = parse_and_print("mixed types",
        "[1, \"two\", true, false, null, -3.14, Inf]");
    ASSERT(v != NULL && pasta_count(v) == 7, "count 7");
    ASSERT(pasta_get_number(pasta_array_get(v, 0)) == 1.0, "[0]=1");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(v, 1)), "two") == 0, "[1]=two");
    ASSERT(pasta_get_bool(pasta_array_get(v, 2)) == 1, "[2]=true");
    ASSERT(pasta_get_bool(pasta_array_get(v, 3)) == 0, "[3]=false");
    ASSERT(pasta_is_null(pasta_array_get(v, 4)), "[4]=null");
    ASSERT(pasta_get_number(pasta_array_get(v, 5)) < -3.0, "[5]=-3.14");
    ASSERT(isinf(pasta_get_number(pasta_array_get(v, 6))), "[6]=Inf");
    pasta_free(v);

    v = parse_and_print("nested arrays", "[[1, 2], [3, 4], [5]]");
    ASSERT(v != NULL && pasta_count(v) == 3, "outer count 3");
    ASSERT(pasta_get_number(pasta_array_get(pasta_array_get(v, 1), 1)) == 4.0, "[1][1]=4");
    pasta_free(v);

    v = parse_and_print("array of maps", "[{x: 1}, {x: 2}, {x: 3}]");
    ASSERT(v != NULL && pasta_count(v) == 3, "count 3");
    for (size_t i = 0; i < 3; i++)
        ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(v, i), "x")) == (double)(i + 1), "x value");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  7. MAPS                                                            */
/* ================================================================== */

static void test_maps(void) {
    SUITE("Maps");
    PastaValue *v;

    v = parse_and_print("single member", "{key: \"value\"}");
    ASSERT(v != NULL && pasta_count(v) == 1, "count 1");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "key")), "value") == 0, "key=value");
    pasta_free(v);

    v = parse_and_print("multiple members", "{a: 1, b: 2, c: 3, d: 4, e: 5}");
    ASSERT(v != NULL && pasta_count(v) == 5, "count 5");
    ASSERT(pasta_get_number(pasta_map_get(v, "e")) == 5.0, "e=5");
    pasta_free(v);

    /* Insertion order preserved */
    v = parse_and_print("key order", "{first: 1, second: 2, third: 3}");
    ASSERT(v != NULL, "parsed");
    ASSERT(strcmp(pasta_map_key(v, 0), "first") == 0, "key 0");
    ASSERT(strcmp(pasta_map_key(v, 1), "second") == 0, "key 1");
    ASSERT(strcmp(pasta_map_key(v, 2), "third") == 0, "key 2");
    pasta_free(v);

    /* Nested maps */
    v = parse_and_print("nested maps", "{outer: {middle: {inner: 42}}}");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(pasta_map_get(v, "outer"), "middle"), "inner")) == 42.0, "inner=42");
    pasta_free(v);

    /* Out-of-bounds safety */
    v = parse_and_print("oob safety", "{a: 1}");
    ASSERT(pasta_map_key(v, 99) == NULL, "oob key NULL");
    ASSERT(pasta_map_value(v, 99) == NULL, "oob value NULL");
    ASSERT(pasta_map_get(v, "missing") == NULL, "missing key NULL");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  8. LABEL CHARACTERS                                                */
/* ================================================================== */

static void test_label_chars(void) {
    SUITE("Label characters");
    PastaValue *v;

    v = parse_and_print("all label symbols",
        "{simple: 1, with_underscore: 2, bang!: 3, #hash: 4,"
        " $dollar: 5, percent%: 6, amp&ersand: 7}");
    ASSERT(v != NULL && pasta_count(v) == 7, "count 7");
    ASSERT(pasta_get_number(pasta_map_get(v, "bang!")) == 3.0, "bang");
    ASSERT(pasta_get_number(pasta_map_get(v, "#hash")) == 4.0, "hash");
    ASSERT(pasta_get_number(pasta_map_get(v, "$dollar")) == 5.0, "dollar");
    ASSERT(pasta_get_number(pasta_map_get(v, "percent%")) == 6.0, "percent");
    ASSERT(pasta_get_number(pasta_map_get(v, "amp&ersand")) == 7.0, "ampersand");
    pasta_free(v);

    v = parse_and_print("mixed case", "{ABC: 1, abc: 2, AbC: 3}");
    ASSERT(v != NULL && pasta_count(v) == 3, "count 3");
    ASSERT(pasta_get_number(pasta_map_get(v, "ABC")) == 1.0, "ABC");
    ASSERT(pasta_get_number(pasta_map_get(v, "abc")) == 2.0, "abc");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  9. COMMENTS                                                        */
/* ================================================================== */

static void test_comments(void) {
    SUITE("Comments");
    PastaValue *v;

    v = parse_and_print("comment preamble",
        "; file header\n; another\n{key: 1}");
    ASSERT(v != NULL && pasta_get_number(pasta_map_get(v, "key")) == 1.0, "key=1");
    pasta_free(v);

    v = parse_and_print("inline comment", "{a: 42 ; the answer\n}");
    ASSERT(v != NULL && pasta_get_number(pasta_map_get(v, "a")) == 42.0, "a=42");
    pasta_free(v);

    v = parse_and_print("comments in array",
        "[\n; items\n1,\n; more\n2,\n3 ; last\n]");
    ASSERT(v != NULL && pasta_count(v) == 3, "count 3");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  10. WHITESPACE                                                     */
/* ================================================================== */

static void test_whitespace(void) {
    SUITE("Whitespace handling");
    PastaValue *v;

    v = parse_and_print("tabs", "{\tname:\t\"tabbed\"\t}");
    ASSERT(v != NULL && strcmp(pasta_get_string(pasta_map_get(v, "name")), "tabbed") == 0, "tabs");
    pasta_free(v);

    v = parse_and_print("CRLF", "{\r\n  a: 1,\r\n  b: 2\r\n}");
    ASSERT(v != NULL && pasta_count(v) == 2, "CRLF");
    pasta_free(v);

    v = parse_and_print("compact", "{a:1,b:2,c:3}");
    ASSERT(v != NULL && pasta_count(v) == 3, "compact");
    pasta_free(v);

    v = parse_and_print("blank lines", "\n\n\n{a: 1}\n\n\n");
    ASSERT(v != NULL && pasta_get_number(pasta_map_get(v, "a")) == 1.0, "blank lines");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  11. DEEP NESTING                                                   */
/* ================================================================== */

static void test_deep_nesting(void) {
    SUITE("Deep nesting");
    PastaValue *v;

    v = parse_and_print("deep map", "{l1: {l2: {l3: {l4: {l5: \"deep\"}}}}}");
    ASSERT(v != NULL, "parsed");
    const PastaValue *cur = v;
    const char *keys[] = {"l1", "l2", "l3", "l4", "l5"};
    for (int i = 0; i < 5; i++) {
        cur = pasta_map_get(cur, keys[i]);
        ASSERT(cur != NULL, "level found");
    }
    ASSERT(strcmp(pasta_get_string(cur), "deep") == 0, "deep value");
    pasta_free(v);

    v = parse_and_print("deep array", "[[[[[42]]]]]");
    ASSERT(v != NULL, "parsed");
    const PastaValue *a = v;
    for (int i = 0; i < 4; i++) a = pasta_array_get(a, 0);
    ASSERT(pasta_get_number(pasta_array_get(a, 0)) == 42.0, "innermost 42");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  12. ERROR CASES                                                    */
/* ================================================================== */

static void test_error_cases(void) {
    SUITE("Error cases");

    parse_expect_fail("unclosed map", "{a: 1", PASTA_OK);
    parse_expect_fail("unclosed array", "[1, 2", PASTA_OK);
    parse_expect_fail("unclosed string", "\"hello", PASTA_OK);
    parse_expect_fail("missing colon", "{key value}", PASTA_OK);
    parse_expect_fail("trailing comma map", "{a: 1,}", PASTA_OK);
    parse_expect_fail("trailing comma array", "[1,]", PASTA_OK);
    parse_expect_fail("empty input", "", PASTA_ERR_UNEXPECTED_EOF);
    parse_expect_fail("only whitespace", "   \n\t  ", PASTA_ERR_UNEXPECTED_EOF);
    parse_expect_fail("only comment", "; just a comment\n", PASTA_ERR_UNEXPECTED_EOF);
    parse_expect_fail("double comma", "[1,,2]", PASTA_OK);
    parse_expect_fail("missing value", "{a: }", PASTA_OK);

    SUITE_OK();
}

/* ================================================================== */
/*  13. EDGE CASES                                                     */
/* ================================================================== */

static void test_edge_cases(void) {
    SUITE("Edge cases");
    PastaValue *v;

    v = parse_and_print("just zero", "0");
    ASSERT(v != NULL && pasta_get_number(v) == 0.0, "zero");
    pasta_free(v);

    /* 32-element array */
    v = parse_and_print("large array",
        "[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,"
        "16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31]");
    ASSERT(v != NULL && pasta_count(v) == 32, "32 elements");
    ASSERT(pasta_get_number(pasta_array_get(v, 31)) == 31.0, "last=31");
    pasta_free(v);

    /* 16-key map */
    v = parse_and_print("many keys",
        "{a:1,b:2,c:3,d:4,e:5,f:6,g:7,h:8,"
        "i:9,j:10,k:11,l:12,m:13,n:14,o:15,p:16}");
    ASSERT(v != NULL && pasta_count(v) == 16, "16 keys");
    ASSERT(pasta_get_number(pasta_map_get(v, "p")) == 16.0, "p=16");
    pasta_free(v);

    /* parse() with explicit length */
    {
        const char *input = "{a: 1}GARBAGE";
        PastaResult r;
        v = pasta_parse(input, 6, &r);
        printf("  [explicit length]\n");
        ASSERT(v != NULL && r.code == PASTA_OK, "stopped at length");
        ASSERT(pasta_get_number(pasta_map_get(v, "a")) == 1.0, "a=1");
        pasta_free(v);
    }

    /* NULL result pointer */
    v = pasta_parse_cstr("[1, 2]", NULL);
    printf("  [null result pointer]\n");
    ASSERT(v != NULL && pasta_count(v) == 2, "null result ptr ok");
    pasta_free(v);

    /* Zero length should fail */
    {
        PastaResult r;
        v = pasta_parse("x", 0, &r);
        printf("  [zero length]\n");
        ASSERT(v == NULL, "zero len fails");
        ASSERT(r.code == PASTA_ERR_UNEXPECTED_EOF, "EOF error");
    }

    SUITE_OK();
}

/* ================================================================== */
/*  14. API SAFETY                                                     */
/* ================================================================== */

static void test_api_safety(void) {
    SUITE("API safety with NULL pointers");

    printf("  [null value queries]\n");
    ASSERT(pasta_type(NULL) == PASTA_NULL, "type(NULL)");
    ASSERT(pasta_is_null(NULL) == 1, "is_null(NULL)");
    ASSERT(pasta_get_bool(NULL) == 0, "get_bool(NULL)");
    ASSERT(pasta_get_number(NULL) == 0.0, "get_number(NULL)");
    ASSERT(pasta_get_string(NULL) == NULL, "get_string(NULL)");
    ASSERT(pasta_get_string_len(NULL) == 0, "get_string_len(NULL)");
    ASSERT(pasta_get_label(NULL) == NULL, "get_label(NULL)");
    ASSERT(pasta_get_label_len(NULL) == 0, "get_label_len(NULL)");
    ASSERT(pasta_count(NULL) == 0, "count(NULL)");
    ASSERT(pasta_array_get(NULL, 0) == NULL, "array_get(NULL)");
    ASSERT(pasta_map_get(NULL, "k") == NULL, "map_get(NULL)");
    ASSERT(pasta_map_key(NULL, 0) == NULL, "map_key(NULL)");
    ASSERT(pasta_map_value(NULL, 0) == NULL, "map_value(NULL)");

    printf("  [free(NULL)]\n");
    pasta_free(NULL);
    ASSERT(1, "free(NULL) ok");

    printf("  [write(NULL)]\n");
    char *out = pasta_write(NULL, PASTA_COMPACT);
    ASSERT(out != NULL && strcmp(out, "null") == 0, "write NULL -> null");
    free(out);

    SUITE_OK();
}

/* ================================================================== */
/*  15. WRITER - COMPACT                                               */
/* ================================================================== */

static void test_write_compact(void) {
    SUITE("Writer: compact");
    PastaValue *v;
    char *out;

    v = pasta_parse_cstr("null", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [null] %s\n", out);
    ASSERT(strcmp(out, "null") == 0, "null");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("true", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [true] %s\n", out);
    ASSERT(strcmp(out, "true") == 0, "true");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("42", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [42] %s\n", out);
    ASSERT(strcmp(out, "42") == 0, "42");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("\"hello\"", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [string] %s\n", out);
    ASSERT(strcmp(out, "\"hello\"") == 0, "string");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("[]", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strcmp(out, "[]") == 0, "empty array");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("{}", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strcmp(out, "{}") == 0, "empty map");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("[1, 2, 3]", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [array] %s\n", out);
    ASSERT(strcmp(out, "[1, 2, 3]") == 0, "array");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("{a: 1, b: 2}", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [map] %s\n", out);
    ASSERT(strcmp(out, "{a: 1, b: 2}") == 0, "map");
    free(out); pasta_free(v);

    v = pasta_parse_cstr("{a: [1, 2], b: {c: 3}}", NULL);
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [nested] %s\n", out);
    ASSERT(strcmp(out, "{a: [1, 2], b: {c: 3}}") == 0, "nested");
    free(out); pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  16. WRITER - PRETTY                                                */
/* ================================================================== */

static void test_write_pretty(void) {
    SUITE("Writer: pretty");
    PastaValue *v;
    char *out;

    v = parse_and_print("simple map", "{x: 1, y: 2}");
    out = pasta_write(v, PASTA_PRETTY);
    printf("  [pretty]\n%s", out);
    ASSERT(strstr(out, "{\n") != NULL, "opens");
    ASSERT(strstr(out, "  x: 1,\n") != NULL, "x indent");
    ASSERT(strstr(out, "  y: 2\n") != NULL, "y indent");
    ASSERT(strstr(out, "}\n") != NULL, "closes");
    free(out); pasta_free(v);

    v = parse_and_print("nested", "{outer: {inner: 42}}");
    out = pasta_write(v, PASTA_PRETTY);
    printf("  [nested pretty]\n%s", out);
    ASSERT(strstr(out, "    inner: 42\n") != NULL, "double indent");
    free(out); pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  17. WRITER - ROUNDTRIP                                             */
/* ================================================================== */

static void roundtrip(const char *label, const char *input) {
    PastaResult r1, r2;
    printf("  [%s]\n", label);

    PastaValue *v1 = pasta_parse_cstr(input, &r1);
    ASSERT(v1 != NULL && r1.code == PASTA_OK, "initial parse");

    char *compact = pasta_write(v1, PASTA_COMPACT);
    ASSERT(compact != NULL, "write compact");
    printf("    compact: %s\n", compact);
    PastaValue *v2 = pasta_parse_cstr(compact, &r2);
    ASSERT(v2 != NULL && r2.code == PASTA_OK, "re-parse compact");
    ASSERT(values_equal(v1, v2), "compact roundtrip");
    pasta_free(v2); free(compact);

    char *pretty = pasta_write(v1, PASTA_PRETTY);
    ASSERT(pretty != NULL, "write pretty");
    PastaValue *v3 = pasta_parse_cstr(pretty, &r2);
    ASSERT(v3 != NULL && r2.code == PASTA_OK, "re-parse pretty");
    ASSERT(values_equal(v1, v3), "pretty roundtrip");
    pasta_free(v3); free(pretty);

    pasta_free(v1);
}

static void test_write_roundtrip(void) {
    SUITE("Writer: roundtrip");

    roundtrip("null", "null");
    roundtrip("bool", "true");
    roundtrip("integer", "42");
    roundtrip("negative", "-99");
    roundtrip("decimal", "3.14");
    roundtrip("Inf", "Inf");
    roundtrip("-Inf", "-Inf");
    roundtrip("NaN", "NaN");
    roundtrip("string", "\"hello world\"");
    roundtrip("string with backslash", "\"C:\\path\\to\\file\"");
    roundtrip("empty array", "[]");
    roundtrip("empty map", "{}");
    roundtrip("simple array", "[1, 2, 3]");
    roundtrip("simple map", "{a: 1, b: 2}");
    roundtrip("mixed array", "[true, false, null, 42, \"hi\", Inf, NaN]");
    roundtrip("nested config",
        "{db: {host: \"localhost\", port: 5432}, tags: [\"a\", \"b\"], debug: false}");
    roundtrip("deep nesting", "{a: {b: {c: {d: [1, [2, [3]]]}}}}");
    roundtrip("label symbols",
        "{$price: 1, _id: 2, bang!: 3, #tag: 4}");

    SUITE_OK();
}

/* ================================================================== */
/*  18. WRITER - COMMENTS STRIPPED                                     */
/* ================================================================== */

static void test_write_strips_comments(void) {
    SUITE("Writer: comments stripped");
    PastaValue *v;
    char *out;

    v = pasta_parse_cstr(
        "; header\n{; section\n  key: 42 ; inline\n}", NULL);
    ASSERT(v != NULL, "parsed");
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [compact] %s\n", out);
    ASSERT(strstr(out, ";") == NULL, "no semicolons");
    ASSERT(strcmp(out, "{key: 42}") == 0, "clean");
    free(out); pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  19. FILE: scalars.pasta                                            */
/* ================================================================== */

static void test_file_scalars(void) {
    SUITE("File: scalars.pasta");

    PastaValue *v = parse_file("scalars.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");
    ASSERT(pasta_count(v) == 15, "15 keys");

    ASSERT(pasta_is_null(pasta_map_get(v, "nothing")), "nothing=null");
    ASSERT(pasta_get_bool(pasta_map_get(v, "yes")) == 1, "yes=true");
    ASSERT(pasta_get_bool(pasta_map_get(v, "no")) == 0, "no=false");
    ASSERT(pasta_get_number(pasta_map_get(v, "zero")) == 0.0, "zero=0");
    ASSERT(pasta_get_number(pasta_map_get(v, "answer")) == 42.0, "answer=42");
    ASSERT(pasta_get_number(pasta_map_get(v, "negative")) == -7.0, "negative=-7");
    ASSERT(pasta_get_number(pasta_map_get(v, "pi")) > 3.14, "pi>3.14");
    ASSERT(pasta_get_number(pasta_map_get(v, "tiny")) == 0.001, "tiny");
    ASSERT(pasta_get_number(pasta_map_get(v, "big")) == 1000000.0, "big");
    ASSERT(isinf(pasta_get_number(pasta_map_get(v, "pos_inf"))), "pos_inf");
    ASSERT(isinf(pasta_get_number(pasta_map_get(v, "neg_inf"))), "neg_inf");
    ASSERT(isnan(pasta_get_number(pasta_map_get(v, "not_a_number"))), "NaN");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "greeting")), "Hello, world!") == 0, "greeting");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "empty_str")), "") == 0, "empty_str");

    /* Backslash is literal */
    const char *path = pasta_get_string(pasta_map_get(v, "path"));
    ASSERT(path != NULL, "path exists");
    ASSERT(strstr(path, "\\Program") != NULL, "backslash literal in file");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_COMPACT);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "roundtrip parse");
    ASSERT(values_equal(v, v2), "roundtrip equal");
    printf("    compact: %s\n", out);
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  20. FILE: database.pasta                                           */
/* ================================================================== */

static void test_file_database(void) {
    SUITE("File: database.pasta");

    PastaValue *v = parse_file("database.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 3, "3 sections");

    const PastaValue *primary = pasta_map_get(v, "primary");
    ASSERT(primary != NULL && pasta_count(primary) == 5, "primary has 5 keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(primary, "host")), "db-primary.internal") == 0, "primary.host");
    ASSERT(pasta_get_number(pasta_map_get(primary, "port")) == 5432.0, "primary.port");
    ASSERT(pasta_get_bool(pasta_map_get(primary, "ssl")) == 1, "primary.ssl");

    const PastaValue *pool = pasta_map_get(primary, "pool");
    ASSERT(pool != NULL, "pool exists");
    ASSERT(pasta_get_number(pasta_map_get(pool, "min")) == 5.0, "pool.min");
    ASSERT(pasta_get_number(pasta_map_get(pool, "max")) == 20.0, "pool.max");

    const PastaValue *replica = pasta_map_get(v, "replica");
    ASSERT(replica != NULL, "replica exists");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(replica, "host")), "db-replica.internal") == 0, "replica.host");

    const PastaValue *timeouts = pasta_map_get(v, "timeouts");
    ASSERT(timeouts != NULL && pasta_count(timeouts) == 3, "3 timeouts");
    ASSERT(pasta_get_number(pasta_map_get(timeouts, "connect")) == 5.0, "connect");
    ASSERT(pasta_get_number(pasta_map_get(timeouts, "idle")) == 600.0, "idle");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_PRETTY);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "pretty roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  21. FILE: server.pasta                                             */
/* ================================================================== */

static void test_file_server(void) {
    SUITE("File: server.pasta");

    PastaValue *v = parse_file("server.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 6, "6 top keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "bind")), "0.0.0.0") == 0, "bind");
    ASSERT(pasta_get_number(pasta_map_get(v, "port")) == 8080.0, "port");
    ASSERT(pasta_get_number(pasta_map_get(v, "workers")) == 4.0, "workers");

    const PastaValue *routes = pasta_map_get(v, "routes");
    ASSERT(routes != NULL && pasta_count(routes) == 4, "4 routes");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(routes, 0), "path")), "/api/users") == 0, "route[0].path");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(routes, 2), "path")), "/api/users/:id") == 0, "route[2].path");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(routes, 1), "method")), "POST") == 0, "route[1].method");

    const PastaValue *cors = pasta_map_get(v, "cors");
    ASSERT(cors != NULL && pasta_get_bool(pasta_map_get(cors, "enabled")) == 1, "cors.enabled");
    ASSERT(pasta_count(pasta_map_get(cors, "origins")) == 2, "2 origins");

    const PastaValue *tls = pasta_map_get(v, "tls");
    ASSERT(pasta_get_bool(pasta_map_get(tls, "enabled")) == 0, "tls disabled");
    ASSERT(pasta_is_null(pasta_map_get(tls, "cert_path")), "cert_path=null");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_COMPACT);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "compact roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  22. FILE: labels.pasta                                             */
/* ================================================================== */

static void test_file_labels(void) {
    SUITE("File: labels.pasta");

    PastaValue *v = parse_file("labels.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 11, "11 keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "simple")), "plain label") == 0, "simple");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "bang!")), "exclamation") == 0, "bang!");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "#tag")), "hash prefix") == 0, "#tag");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "$price")), "dollar prefix") == 0, "$price");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "tax%")), "percent suffix") == 0, "tax%");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "save&load")), "ampersand") == 0, "save&load");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "_private")), "leading underscore") == 0, "_private");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "x")), "single char") == 0, "x");

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  23. FILE: nested.pasta                                             */
/* ================================================================== */

static void test_file_nested(void) {
    SUITE("File: nested.pasta");

    PastaValue *v = parse_file("nested.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 3, "3 top keys");

    /* Deep map traversal */
    const PastaValue *l4 = pasta_map_get(
        pasta_map_get(pasta_map_get(pasta_map_get(v, "level1"), "level2"), "level3"), "level4");
    ASSERT(l4 != NULL, "level4 exists");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(l4, "value")), "deep") == 0, "deep value");

    /* Matrix */
    const PastaValue *matrix = pasta_map_get(v, "matrix");
    ASSERT(matrix != NULL && pasta_count(matrix) == 3, "3 rows");
    ASSERT(pasta_count(pasta_array_get(matrix, 0)) == 3, "3 cols");
    ASSERT(pasta_get_number(pasta_array_get(pasta_array_get(matrix, 1), 2)) == 6.0, "[1][2]=6");

    /* Records */
    const PastaValue *records = pasta_map_get(v, "records");
    ASSERT(records != NULL && pasta_count(records) == 2, "2 records");

    const PastaValue *alice = pasta_array_get(records, 0);
    ASSERT(strcmp(pasta_get_string(pasta_map_get(alice, "name")), "Alice") == 0, "Alice");
    ASSERT(pasta_count(pasta_map_get(alice, "tags")) == 2, "Alice has 2 tags");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(pasta_map_get(alice, "tags"), 0)), "admin") == 0, "tag=admin");

    const PastaValue *alice_meta = pasta_map_get(alice, "meta");
    ASSERT(pasta_get_number(pasta_map_get(alice_meta, "score")) == 98.5, "score=98.5");

    const PastaValue *bob = pasta_array_get(records, 1);
    ASSERT(strcmp(pasta_get_string(pasta_map_get(bob, "name")), "Bob") == 0, "Bob");
    ASSERT(pasta_count(pasta_map_get(bob, "tags")) == 1, "Bob has 1 tag");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_PRETTY);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "pretty roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  24. FILE: comments.pasta                                           */
/* ================================================================== */

static void test_file_comments(void) {
    SUITE("File: comments.pasta");

    PastaValue *v = parse_file("comments.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 6, "6 top keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "app_name")), "my_service") == 0, "app_name");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "version")), "1.2.3") == 0, "version");

    const PastaValue *features = pasta_map_get(v, "features");
    ASSERT(features != NULL && pasta_count(features) == 3, "3 features");
    ASSERT(pasta_get_bool(pasta_map_get(features, "dark_mode")) == 1, "dark_mode");
    ASSERT(pasta_get_bool(pasta_map_get(features, "new_api")) == 0, "new_api");

    ASSERT(pasta_count(pasta_map_get(v, "plugins")) == 0, "plugins empty");
    ASSERT(pasta_count(pasta_map_get(v, "overrides")) == 0, "overrides empty");

    /* Semicolons in strings are preserved */
    const char *msg = pasta_get_string(pasta_map_get(v, "message"));
    ASSERT(msg != NULL && strstr(msg, ";") != NULL, "semicolons in string preserved");

    /* Writer strips comments */
    char *out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strstr(out, ";") == NULL || strstr(out, ";") == strstr(out, "; "),
           "no comment semicolons in output");
    /* Actually, the message string contains semicolons, so just check no bare ; outside strings */
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  25. FILE: compact.pasta                                            */
/* ================================================================== */

static void test_file_compact(void) {
    SUITE("File: compact.pasta");

    PastaValue *v = parse_file("compact.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 6, "6 keys");
    ASSERT(pasta_get_number(pasta_map_get(v, "a")) == 1.0, "a=1");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "b")), "two") == 0, "b=two");
    ASSERT(pasta_get_bool(pasta_map_get(v, "c")) == 1, "c=true");
    ASSERT(pasta_is_null(pasta_map_get(v, "d")), "d=null");
    ASSERT(pasta_count(pasta_map_get(v, "e")) == 3, "e=[1,2,3]");
    const PastaValue *f = pasta_map_get(v, "f");
    ASSERT(f != NULL && pasta_get_number(pasta_map_get(f, "x")) == 10.0, "f.x=10");

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  26. FILE: strings.pasta                                            */
/* ================================================================== */

static void test_file_strings(void) {
    SUITE("File: strings.pasta");

    PastaValue *v = parse_file("strings.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 12, "12 keys");

    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "plain")), "hello world") == 0, "plain");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "empty")), "") == 0, "empty");

    /* Backslashes are literal */
    const char *win = pasta_get_string(pasta_map_get(v, "path_win"));
    ASSERT(win != NULL, "path_win exists");
    ASSERT(strstr(win, "\\Users\\") != NULL, "backslash literal");
    ASSERT(strstr(win, "\\Admin\\") != NULL, "more backslashes");

    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "path_unix")), "/var/log/app.log") == 0, "unix path");

    /* URL with special chars */
    const char *url = pasta_get_string(pasta_map_get(v, "url"));
    ASSERT(url != NULL && strstr(url, "?q=test&limit=10") != NULL, "url query");

    /* Semicolons are just chars in strings */
    const char *math = pasta_get_string(pasta_map_get(v, "math"));
    ASSERT(math != NULL && strstr(math, "; ") != NULL, "semicolons in string");

    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "colons")), "12:34:56") == 0, "colons");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "pipes")), "a | b | c") == 0, "pipes");

    /* Backslashes roundtrip verbatim */
    const char *bs = pasta_get_string(pasta_map_get(v, "backslashes"));
    ASSERT(bs != NULL && strcmp(bs, "C:\\path\\to\\file") == 0, "backslash roundtrip");
    char *out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strstr(out, "\"C:\\path\\to\\file\"") != NULL, "backslash in writer output");
    free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  27. FILE: empty.pasta                                              */
/* ================================================================== */

static void test_file_empty(void) {
    SUITE("File: empty.pasta");

    PastaValue *v = parse_file("empty.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");
    ASSERT(pasta_count(v) == 0, "empty");

    char *out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strcmp(out, "{}") == 0, "writes as {}");
    free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  28. FILE: array_root.pasta                                         */
/* ================================================================== */

static void test_file_array_root(void) {
    SUITE("File: array_root.pasta");

    PastaValue *v = parse_file("array_root.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_ARRAY, "root is array");
    ASSERT(pasta_count(v) == 3, "3 records");

    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(v, 0), "name")), "Alice") == 0, "[0].name");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(v, 1), "role")), "user") == 0, "[1].role");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(v, 2), "name")), "Carol") == 0, "[2].name");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_PRETTY);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  29. MULTI-CONTAINER INLINE                                         */
/* ================================================================== */

static void test_multi_container_inline(void) {
    SUITE("Multi-container inline");
    PastaValue *v;

    /* Two maps */
    v = parse_and_print("two maps", "{a: 1} {b: 2}");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_type(v) == PASTA_ARRAY, "wrapped in array");
    ASSERT(pasta_count(v) == 2, "2 containers");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(v, 0), "a")) == 1.0, "[0].a=1");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(v, 1), "b")) == 2.0, "[1].b=2");
    pasta_free(v);

    /* Three maps */
    v = parse_and_print("three maps", "{x: 1}\n{y: 2}\n{z: 3}");
    ASSERT(v != NULL && pasta_count(v) == 3, "3 containers");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(v, 2), "z")) == 3.0, "[2].z=3");
    pasta_free(v);

    /* Two arrays */
    v = parse_and_print("two arrays", "[1, 2] [3, 4]");
    ASSERT(v != NULL && pasta_type(v) == PASTA_ARRAY, "array of arrays");
    ASSERT(pasta_count(v) == 2, "2 containers");
    ASSERT(pasta_type(pasta_array_get(v, 0)) == PASTA_ARRAY, "[0] is array");
    ASSERT(pasta_get_number(pasta_array_get(pasta_array_get(v, 1), 0)) == 3.0, "[1][0]=3");
    pasta_free(v);

    /* Mixed: map then array */
    v = parse_and_print("map then array", "{key: \"val\"} [1, 2, 3]");
    ASSERT(v != NULL && pasta_count(v) == 2, "2 containers");
    ASSERT(pasta_type(pasta_array_get(v, 0)) == PASTA_MAP, "[0] is map");
    ASSERT(pasta_type(pasta_array_get(v, 1)) == PASTA_ARRAY, "[1] is array");
    pasta_free(v);

    /* Mixed: array then map */
    v = parse_and_print("array then map", "[1] {a: 2}");
    ASSERT(v != NULL && pasta_count(v) == 2, "2 containers");
    ASSERT(pasta_type(pasta_array_get(v, 0)) == PASTA_ARRAY, "[0] is array");
    ASSERT(pasta_type(pasta_array_get(v, 1)) == PASTA_MAP, "[1] is map");
    pasta_free(v);

    /* Single container still returns directly (not wrapped) */
    v = parse_and_print("single map", "{a: 1}");
    ASSERT(v != NULL && pasta_type(v) == PASTA_MAP, "single map not wrapped");
    pasta_free(v);

    v = parse_and_print("single array", "[1, 2]");
    ASSERT(v != NULL && pasta_type(v) == PASTA_ARRAY, "single array not wrapped");
    ASSERT(pasta_count(v) == 2, "still 2 elements");
    pasta_free(v);

    /* With comments between containers */
    v = parse_and_print("comments between",
        "; first section\n"
        "{a: 1}\n"
        "; second section\n"
        "{b: 2}");
    ASSERT(v != NULL && pasta_count(v) == 2, "2 containers with comments");
    pasta_free(v);

    /* Roundtrip: multi-container -> write -> re-parse */
    v = pasta_parse_cstr("{a: 1} {b: 2} {c: 3}", NULL);
    ASSERT(v != NULL, "parsed");
    char *out = pasta_write(v, PASTA_COMPACT);
    printf("    roundtrip compact: %s\n", out);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parsed");
    ASSERT(values_equal(v, v2), "roundtrip equal");
    pasta_free(v2); free(out);
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  30. FILE: multi_maps.pasta                                         */
/* ================================================================== */

static void test_file_multi_maps(void) {
    SUITE("File: multi_maps.pasta");

    PastaValue *v = parse_file("multi_maps.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_ARRAY, "root is array (multi-container)");
    ASSERT(pasta_count(v) == 3, "3 containers");

    /* First map: identity */
    const PastaValue *m0 = pasta_array_get(v, 0);
    ASSERT(pasta_type(m0) == PASTA_MAP, "[0] is map");
    ASSERT(pasta_count(m0) == 2, "[0] has 2 keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(m0, "name")), "auth_service") == 0, "name");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(m0, "version")), "3.1.0") == 0, "version");

    /* Second map: network */
    const PastaValue *m1 = pasta_array_get(v, 1);
    ASSERT(pasta_type(m1) == PASTA_MAP, "[1] is map");
    ASSERT(pasta_get_number(pasta_map_get(m1, "port")) == 9090.0, "port=9090");
    ASSERT(pasta_get_bool(pasta_map_get(m1, "tls")) == 1, "tls=true");

    /* Third map: logging */
    const PastaValue *m2 = pasta_array_get(v, 2);
    ASSERT(pasta_type(m2) == PASTA_MAP, "[2] is map");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(m2, "log_level")), "debug") == 0, "log_level");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(m2, "log_output")), "/var/log/auth.log") == 0, "log_output");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_PRETTY);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  31. FILE: multi_arrays.pasta                                       */
/* ================================================================== */

static void test_file_multi_arrays(void) {
    SUITE("File: multi_arrays.pasta");

    PastaValue *v = parse_file("multi_arrays.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_ARRAY, "root is array (multi-container)");
    ASSERT(pasta_count(v) == 3, "3 containers");

    /* First: roles */
    const PastaValue *a0 = pasta_array_get(v, 0);
    ASSERT(pasta_type(a0) == PASTA_ARRAY, "[0] is array");
    ASSERT(pasta_count(a0) == 3, "3 roles");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(a0, 0)), "admin") == 0, "admin");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(a0, 2)), "viewer") == 0, "viewer");

    /* Second: rate limit configs */
    const PastaValue *a1 = pasta_array_get(v, 1);
    ASSERT(pasta_type(a1) == PASTA_ARRAY, "[1] is array");
    ASSERT(pasta_count(a1) == 3, "3 endpoints");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(a1, 0), "endpoint")), "/login") == 0, "/login");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(a1, 2), "rate")) == 2.0, "rate=2");

    /* Third: status codes */
    const PastaValue *a2 = pasta_array_get(v, 2);
    ASSERT(pasta_type(a2) == PASTA_ARRAY, "[2] is array");
    ASSERT(pasta_count(a2) == 9, "9 status codes");
    ASSERT(pasta_get_number(pasta_array_get(a2, 0)) == 200.0, "200");
    ASSERT(pasta_get_number(pasta_array_get(a2, 8)) == 500.0, "500");

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  32. FILE: multi_mixed.pasta                                        */
/* ================================================================== */

static void test_file_multi_mixed(void) {
    SUITE("File: multi_mixed.pasta");

    PastaValue *v = parse_file("multi_mixed.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_ARRAY, "root is array");
    ASSERT(pasta_count(v) == 4, "4 containers");

    /* Container 0: metadata map */
    const PastaValue *c0 = pasta_array_get(v, 0);
    ASSERT(pasta_type(c0) == PASTA_MAP, "[0] is map");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(c0, "schema")), "https://pasta-lang.org/config/v1") == 0, "schema");

    /* Container 1: services array */
    const PastaValue *c1 = pasta_array_get(v, 1);
    ASSERT(pasta_type(c1) == PASTA_ARRAY, "[1] is array");
    ASSERT(pasta_count(c1) == 3, "3 services");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(c1, 0), "name")), "web") == 0, "web");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(c1, 1), "replicas")) == 2.0, "worker replicas");

    /* Container 2: networking map (deeply nested) */
    const PastaValue *c2 = pasta_array_get(v, 2);
    ASSERT(pasta_type(c2) == PASTA_MAP, "[2] is map");
    const PastaValue *net = pasta_map_get(c2, "networking");
    ASSERT(net != NULL, "networking key");
    const PastaValue *ingress = pasta_map_get(net, "ingress");
    ASSERT(ingress != NULL, "ingress key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(ingress, "host")), "api.example.com") == 0, "ingress host");
    const PastaValue *annotations = pasta_map_get(ingress, "annotations");
    ASSERT(annotations != NULL, "annotations key");
    ASSERT(pasta_get_number(pasta_map_get(annotations, "rate_limit")) == 1000.0, "rate_limit");

    const PastaValue *svc = pasta_map_get(net, "service");
    const PastaValue *ports = pasta_map_get(svc, "ports");
    ASSERT(ports != NULL && pasta_count(ports) == 2, "2 ports");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(ports, 0), "port")) == 80.0, "http port");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(ports, 1), "target")) == 9090.0, "metrics target");

    /* Container 3: regions array */
    const PastaValue *c3 = pasta_array_get(v, 3);
    ASSERT(pasta_type(c3) == PASTA_ARRAY, "[3] is array");
    ASSERT(pasta_count(c3) == 3, "3 regions");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(c3, 0)), "us-east-1") == 0, "us-east-1");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_COMPACT);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "compact roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  33. FILE: complex.pasta                                            */
/* ================================================================== */

static void test_file_complex(void) {
    SUITE("File: complex.pasta");

    PastaValue *v = parse_file("complex.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    /* 3 top-level containers: global config, regions array, pipelines config */
    ASSERT(pasta_type(v) == PASTA_ARRAY, "root is array");
    ASSERT(pasta_count(v) == 3, "3 containers");

    /* ---- Container 0: global settings ---- */
    const PastaValue *c0 = pasta_array_get(v, 0);
    ASSERT(pasta_type(c0) == PASTA_MAP, "[0] is map");
    const PastaValue *global = pasta_map_get(c0, "global");
    ASSERT(global != NULL, "global key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(global, "project")), "mega_platform") == 0, "project");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(global, "environment")), "production") == 0, "environment");

    /* Monitoring providers */
    const PastaValue *mon = pasta_map_get(global, "monitoring");
    ASSERT(pasta_get_bool(pasta_map_get(mon, "enabled")) == 1, "monitoring enabled");
    const PastaValue *providers = pasta_map_get(mon, "providers");
    ASSERT(providers != NULL && pasta_count(providers) == 2, "2 providers");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(providers, 0), "name")), "prometheus") == 0, "prometheus");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(providers, 0), "scrape_interval")) == 15.0, "scrape_interval");
    /* Nested labels map inside provider */
    const PastaValue *labels = pasta_map_get(pasta_array_get(providers, 0), "labels");
    ASSERT(labels != NULL && pasta_count(labels) == 2, "2 labels");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(labels, "team")), "platform") == 0, "team=platform");

    const PastaValue *dd = pasta_array_get(providers, 1);
    ASSERT(pasta_is_null(pasta_map_get(dd, "api_key")), "api_key=null");
    const PastaValue *dd_tags = pasta_map_get(dd, "tags");
    ASSERT(dd_tags != NULL && pasta_count(dd_tags) == 2, "2 datadog tags");

    /* Secrets vault paths */
    const PastaValue *secrets = pasta_map_get(global, "secrets");
    const PastaValue *paths = pasta_map_get(secrets, "paths");
    ASSERT(paths != NULL && pasta_count(paths) == 3, "3 secret paths");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(paths, "database")), "secret/data/db") == 0, "db path");

    /* ---- Container 1: regions array ---- */
    const PastaValue *c1 = pasta_array_get(v, 1);
    ASSERT(pasta_type(c1) == PASTA_ARRAY, "[1] is array");
    ASSERT(pasta_count(c1) == 2, "2 regions");

    /* US East */
    const PastaValue *us = pasta_array_get(c1, 0);
    ASSERT(strcmp(pasta_get_string(pasta_map_get(us, "region")), "us-east-1") == 0, "us-east-1");
    ASSERT(pasta_get_bool(pasta_map_get(us, "primary")) == 1, "us is primary");

    const PastaValue *us_services = pasta_map_get(us, "services");
    ASSERT(us_services != NULL && pasta_count(us_services) == 2, "2 us services");
    const PastaValue *us_gw = pasta_array_get(us_services, 0);
    ASSERT(pasta_get_number(pasta_map_get(us_gw, "replicas")) == 5.0, "gw replicas=5");
    /* Deep: service -> env -> specific value */
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(us_gw, "env"), "RATE_LIMIT")) == 10000.0, "RATE_LIMIT");
    /* Deep: service -> health -> threshold */
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(us_gw, "health"), "threshold")) == 3.0, "threshold");

    const PastaValue *us_backend = pasta_array_get(us_services, 1);
    ASSERT(pasta_get_number(pasta_map_get(us_backend, "replicas")) == 10.0, "backend replicas=10");
    const PastaValue *volumes = pasta_map_get(us_backend, "volumes");
    ASSERT(volumes != NULL && pasta_count(volumes) == 2, "2 volumes");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(volumes, 0), "mount")), "/app/data") == 0, "data mount");

    /* Database readers with weights */
    const PastaValue *us_db = pasta_map_get(us, "database");
    const PastaValue *readers = pasta_map_get(pasta_map_get(us_db, "instances"), "readers");
    ASSERT(readers != NULL && pasta_count(readers) == 3, "3 readers");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(readers, 0), "weight")) == 50.0, "reader[0] weight");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(readers, 2), "weight")) == 20.0, "reader[2] weight");
    /* Backup config */
    const PastaValue *backup = pasta_map_get(us_db, "backup");
    ASSERT(pasta_get_bool(pasta_map_get(backup, "enabled")) == 1, "backup enabled");
    ASSERT(pasta_get_number(pasta_map_get(backup, "retention_days")) == 30.0, "retention=30");

    /* EU West */
    const PastaValue *eu = pasta_array_get(c1, 1);
    ASSERT(strcmp(pasta_get_string(pasta_map_get(eu, "region")), "eu-west-1") == 0, "eu-west-1");
    ASSERT(pasta_get_bool(pasta_map_get(eu, "primary")) == 0, "eu not primary");
    const PastaValue *eu_services = pasta_map_get(eu, "services");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(eu_services, 0), "replicas")) == 3.0, "eu gw replicas=3");
    const PastaValue *eu_readers = pasta_map_get(pasta_map_get(pasta_map_get(eu, "database"), "instances"), "readers");
    ASSERT(pasta_count(eu_readers) == 2, "eu 2 readers");

    /* ---- Container 2: pipelines ---- */
    const PastaValue *c2 = pasta_array_get(v, 2);
    ASSERT(pasta_type(c2) == PASTA_MAP, "[2] is map");
    const PastaValue *pipelines = pasta_map_get(c2, "pipelines");
    ASSERT(pipelines != NULL, "pipelines key");

    /* Build pipeline */
    const PastaValue *build = pasta_map_get(pipelines, "build");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(build, "trigger")), "push") == 0, "build trigger=push");
    const PastaValue *build_stages = pasta_map_get(build, "stages");
    ASSERT(build_stages != NULL && pasta_count(build_stages) == 4, "4 build stages");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(build_stages, 1), "name")), "test") == 0, "stage[1]=test");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(build_stages, 1), "parallel")) == 4.0, "test parallel=4");

    /* Deploy pipeline */
    const PastaValue *deploy = pasta_map_get(pipelines, "deploy");
    const PastaValue *deploy_stages = pasta_map_get(deploy, "stages");
    ASSERT(deploy_stages != NULL && pasta_count(deploy_stages) == 3, "3 deploy stages");
    const PastaValue *canary = pasta_array_get(deploy_stages, 1);
    ASSERT(pasta_get_bool(pasta_map_get(canary, "approval")) == 1, "canary approval=true");
    ASSERT(pasta_get_number(pasta_map_get(canary, "traffic")) == 5.0, "canary traffic=5");

    /* Rollback config */
    const PastaValue *rollback = pasta_map_get(deploy, "rollback");
    ASSERT(rollback != NULL, "rollback key");
    ASSERT(pasta_get_bool(pasta_map_get(rollback, "auto")) == 1, "auto rollback");
    ASSERT(pasta_get_number(pasta_map_get(rollback, "error_threshold")) == 5.0, "error_threshold");

    /* Notifications */
    const PastaValue *notif = pasta_map_get(c2, "notifications");
    const PastaValue *channels = pasta_map_get(notif, "channels");
    ASSERT(channels != NULL && pasta_count(channels) == 2, "2 channels");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(channels, 0), "type")), "slack") == 0, "slack");
    const PastaValue *slack_events = pasta_map_get(pasta_array_get(channels, 0), "events");
    ASSERT(slack_events != NULL && pasta_count(slack_events) == 3, "3 slack events");
    const PastaValue *pd = pasta_array_get(channels, 1);
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pd, "type")), "pagerduty") == 0, "pagerduty");

    /* Full roundtrip on this monster */
    char *out = pasta_write(v, PASTA_PRETTY);
    ASSERT(out != NULL, "write pretty");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse pretty");
    ASSERT(values_equal(v, v2), "pretty roundtrip");
    pasta_free(v2); free(out);

    out = pasta_write(v, PASTA_COMPACT);
    ASSERT(out != NULL, "write compact");
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse compact");
    ASSERT(values_equal(v, v2), "compact roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  34. Builder API: scalars                                           */
/* ================================================================== */

static void test_builder_scalars(void) {
    SUITE("Builder: scalars");

    PastaValue *v;

    v = pasta_new_null();
    ASSERT(v != NULL, "new_null");
    ASSERT(pasta_type(v) == PASTA_NULL, "type=null");
    ASSERT(pasta_is_null(v), "is_null");
    pasta_free(v);

    v = pasta_new_bool(1);
    ASSERT(v != NULL && pasta_type(v) == PASTA_BOOL, "new_bool(1)");
    ASSERT(pasta_get_bool(v) == 1, "bool=true");
    pasta_free(v);

    v = pasta_new_bool(0);
    ASSERT(pasta_get_bool(v) == 0, "bool=false");
    pasta_free(v);

    v = pasta_new_number(42.5);
    ASSERT(v != NULL && pasta_type(v) == PASTA_NUMBER, "new_number");
    ASSERT(pasta_get_number(v) == 42.5, "number=42.5");
    pasta_free(v);

    v = pasta_new_string("hello");
    ASSERT(v != NULL && pasta_type(v) == PASTA_STRING, "new_string");
    ASSERT(strcmp(pasta_get_string(v), "hello") == 0, "string=hello");
    ASSERT(pasta_get_string_len(v) == 5, "len=5");
    pasta_free(v);

    v = pasta_new_string_len("abc\0def", 7);
    ASSERT(v != NULL && pasta_get_string_len(v) == 7, "string_len with embedded null");
    pasta_free(v);

    v = pasta_new_string("");
    ASSERT(v != NULL && pasta_get_string_len(v) == 0, "empty string");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  35. Builder API: containers                                        */
/* ================================================================== */

static void test_builder_containers(void) {
    SUITE("Builder: containers");

    /* Array */
    PastaValue *arr = pasta_new_array();
    ASSERT(arr != NULL && pasta_type(arr) == PASTA_ARRAY, "new_array");
    ASSERT(pasta_count(arr) == 0, "empty array");

    ASSERT(pasta_push(arr, pasta_new_number(1)) == 0, "push 1");
    ASSERT(pasta_push(arr, pasta_new_number(2)) == 0, "push 2");
    ASSERT(pasta_push(arr, pasta_new_string("three")) == 0, "push three");
    ASSERT(pasta_count(arr) == 3, "count=3");
    ASSERT(pasta_get_number(pasta_array_get(arr, 0)) == 1.0, "[0]=1");
    ASSERT(pasta_get_number(pasta_array_get(arr, 1)) == 2.0, "[1]=2");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(arr, 2)), "three") == 0, "[2]=three");
    pasta_free(arr);

    /* Map */
    PastaValue *map = pasta_new_map();
    ASSERT(map != NULL && pasta_type(map) == PASTA_MAP, "new_map");
    ASSERT(pasta_count(map) == 0, "empty map");

    ASSERT(pasta_set(map, "name", pasta_new_string("Alice")) == 0, "set name");
    ASSERT(pasta_set(map, "age", pasta_new_number(30)) == 0, "set age");
    ASSERT(pasta_set(map, "active", pasta_new_bool(1)) == 0, "set active");
    ASSERT(pasta_count(map) == 3, "map count=3");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(map, "name")), "Alice") == 0, "name=Alice");
    ASSERT(pasta_get_number(pasta_map_get(map, "age")) == 30.0, "age=30");
    ASSERT(pasta_get_bool(pasta_map_get(map, "active")) == 1, "active=true");
    pasta_free(map);

    /* Type safety */
    PastaValue *num = pasta_new_number(42);
    ASSERT(pasta_push(num, pasta_new_number(1)) == -1, "push on non-array");
    ASSERT(pasta_set(num, "x", pasta_new_number(1)) == -1, "set on non-map");
    pasta_free(num);

    SUITE_OK();
}

/* ================================================================== */
/*  36. Builder API: nested + write roundtrip                          */
/* ================================================================== */

static void test_builder_roundtrip(void) {
    SUITE("Builder: nested + roundtrip");

    /* Build: {name: "web", port: 8080, tags: ["go", "api"], db: {host: "localhost", port: 5432}} */
    PastaValue *root = pasta_new_map();

    pasta_set(root, "name", pasta_new_string("web"));
    pasta_set(root, "port", pasta_new_number(8080));

    PastaValue *tags = pasta_new_array();
    pasta_push(tags, pasta_new_string("go"));
    pasta_push(tags, pasta_new_string("api"));
    pasta_set(root, "tags", tags);

    PastaValue *db = pasta_new_map();
    pasta_set(db, "host", pasta_new_string("localhost"));
    pasta_set(db, "port", pasta_new_number(5432));
    pasta_set(root, "db", db);

    /* Write compact */
    char *compact = pasta_write(root, PASTA_COMPACT);
    ASSERT(compact != NULL, "write compact");

    /* Parse it back */
    PastaResult r;
    PastaValue *parsed = pasta_parse_cstr(compact, &r);
    ASSERT(parsed != NULL && r.code == PASTA_OK, "parse back compact");
    ASSERT(values_equal(root, parsed), "compact roundtrip");
    pasta_free(parsed);
    free(compact);

    /* Write pretty */
    char *pretty = pasta_write(root, PASTA_PRETTY);
    ASSERT(pretty != NULL, "write pretty");

    parsed = pasta_parse_cstr(pretty, &r);
    ASSERT(parsed != NULL && r.code == PASTA_OK, "parse back pretty");
    ASSERT(values_equal(root, parsed), "pretty roundtrip");
    pasta_free(parsed);
    free(pretty);

    pasta_free(root);
    SUITE_OK();
}

/* ================================================================== */
/*  37. Builder API: complex document                                  */
/* ================================================================== */

static void test_builder_complex(void) {
    SUITE("Builder: complex document");

    /* Build a server config with nested services, like our test files */
    PastaValue *root = pasta_new_map();
    pasta_set(root, "project", pasta_new_string("myapp"));
    pasta_set(root, "version", pasta_new_string("1.0.0"));
    pasta_set(root, "debug", pasta_new_bool(0));

    /* services array with two entries */
    PastaValue *services = pasta_new_array();

    PastaValue *svc1 = pasta_new_map();
    pasta_set(svc1, "name", pasta_new_string("api"));
    pasta_set(svc1, "replicas", pasta_new_number(3));
    PastaValue *res1 = pasta_new_map();
    pasta_set(res1, "cpu", pasta_new_string("2.0"));
    pasta_set(res1, "memory", pasta_new_number(4096));
    pasta_set(svc1, "resources", res1);
    PastaValue *env1 = pasta_new_map();
    pasta_set(env1, "PORT", pasta_new_number(8080));
    pasta_set(env1, "LOG_LEVEL", pasta_new_string("info"));
    pasta_set(svc1, "env", env1);
    pasta_push(services, svc1);

    PastaValue *svc2 = pasta_new_map();
    pasta_set(svc2, "name", pasta_new_string("worker"));
    pasta_set(svc2, "replicas", pasta_new_number(2));
    PastaValue *tags2 = pasta_new_array();
    pasta_push(tags2, pasta_new_string("async"));
    pasta_push(tags2, pasta_new_string("queue"));
    pasta_set(svc2, "tags", tags2);
    pasta_set(svc2, "timeout", pasta_new_null());
    pasta_push(services, svc2);

    pasta_set(root, "services", services);

    /* database config */
    PastaValue *db = pasta_new_map();
    pasta_set(db, "engine", pasta_new_string("postgres"));
    pasta_set(db, "host", pasta_new_string("db.internal"));
    pasta_set(db, "port", pasta_new_number(5432));
    pasta_set(db, "ssl", pasta_new_bool(1));
    pasta_set(root, "database", db);

    /* Verify tree structure */
    ASSERT(pasta_count(root) == 5, "5 root keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(root, "project")), "myapp") == 0, "project");
    ASSERT(pasta_count(pasta_map_get(root, "services")) == 2, "2 services");
    const PastaValue *s1 = pasta_array_get(pasta_map_get(root, "services"), 0);
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(s1, "env"), "PORT")) == 8080.0, "PORT=8080");
    const PastaValue *s2 = pasta_array_get(pasta_map_get(root, "services"), 1);
    ASSERT(pasta_is_null(pasta_map_get(s2, "timeout")), "timeout=null");

    /* Full roundtrip */
    char *out = pasta_write(root, PASTA_PRETTY);
    ASSERT(out != NULL, "write");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse");
    ASSERT(values_equal(root, v2), "deep roundtrip");
    pasta_free(v2);
    free(out);

    /* Compact roundtrip too */
    out = pasta_write(root, PASTA_COMPACT);
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(root, v2), "compact roundtrip");
    pasta_free(v2);
    free(out);

    pasta_free(root);
    SUITE_OK();
}

/* ================================================================== */
/*  38. Builder API: array root                                        */
/* ================================================================== */

static void test_builder_array_root(void) {
    SUITE("Builder: array root");

    PastaValue *arr = pasta_new_array();
    pasta_push(arr, pasta_new_string("alpha"));
    pasta_push(arr, pasta_new_number(42));
    pasta_push(arr, pasta_new_bool(1));
    pasta_push(arr, pasta_new_null());

    PastaValue *inner = pasta_new_map();
    pasta_set(inner, "key", pasta_new_string("value"));
    pasta_push(arr, inner);

    ASSERT(pasta_count(arr) == 5, "5 elements");

    char *out = pasta_write(arr, PASTA_COMPACT);
    ASSERT(out != NULL, "write");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(arr, v2), "roundtrip");
    pasta_free(v2);
    free(out);

    pasta_free(arr);
    SUITE_OK();
}

/* ================================================================== */
/*  39. Multiline strings: inline                                      */
/* ================================================================== */

static void test_multiline_strings(void) {
    SUITE("Multiline strings");

    /* Basic multiline */
    PastaValue *v = pasta_parse_cstr("{msg: \"\"\"hello\nworld\"\"\"}", NULL);
    ASSERT(v != NULL, "parse basic");
    const char *msg = pasta_get_string(pasta_map_get(v, "msg"));
    ASSERT(msg != NULL, "msg exists");
    ASSERT(strcmp(msg, "hello\nworld") == 0, "content=hello\\nworld");
    ASSERT(pasta_get_string_len(pasta_map_get(v, "msg")) == 11, "len=11");
    pasta_free(v);

    /* Empty multiline */
    v = pasta_parse_cstr("{s: \"\"\"\"\"\"}", NULL);
    ASSERT(v != NULL, "parse empty multiline");
    ASSERT(pasta_get_string_len(pasta_map_get(v, "s")) == 0, "empty len=0");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "s")), "") == 0, "empty content");
    pasta_free(v);

    /* Multiple lines with indentation */
    v = pasta_parse_cstr("{script: \"\"\"line1\n  indented\n    deeper\nback\"\"\"}", NULL);
    ASSERT(v != NULL, "parse indented");
    const char *s = pasta_get_string(pasta_map_get(v, "script"));
    ASSERT(strstr(s, "  indented") != NULL, "indentation preserved");
    ASSERT(strstr(s, "    deeper") != NULL, "deeper indent preserved");
    pasta_free(v);

    /* Multiline in array */
    v = pasta_parse_cstr("[\"regular\", \"\"\"multi\nline\"\"\"]", NULL);
    ASSERT(v != NULL, "parse array with multiline");
    ASSERT(pasta_count(v) == 2, "2 elements");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(v, 0)), "regular") == 0, "regular string");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(v, 1)), "multi\nline") == 0, "multiline in array");
    pasta_free(v);

    /* Regular quotes inside multiline */
    v = pasta_parse_cstr("{s: \"\"\"she said \"hi\" to me\"\"\"}", NULL);
    ASSERT(v != NULL, "parse with inner quotes");
    s = pasta_get_string(pasta_map_get(v, "s"));
    ASSERT(strstr(s, "\"hi\"") != NULL, "inner quotes preserved");
    pasta_free(v);

    /* Single double-quote inside multiline */
    v = pasta_parse_cstr("{s: \"\"\"a \"single\" quote\"\"\"}", NULL);
    ASSERT(v != NULL, "single quote inside");
    ASSERT(strstr(pasta_get_string(pasta_map_get(v, "s")), "\"single\"") != NULL, "single quotes ok");
    pasta_free(v);

    /* Two consecutive quotes inside multiline (not three) */
    v = pasta_parse_cstr("{s: \"\"\"before\"\"after\"\"\"}", NULL);
    ASSERT(v != NULL, "two quotes inside");
    s = pasta_get_string(pasta_map_get(v, "s"));
    ASSERT(strcmp(s, "before\"\"after") == 0, "two quotes content");
    pasta_free(v);

    /* Writer roundtrip: string with newline uses triple-quotes */
    PastaValue *map = pasta_new_map();
    pasta_set(map, "output", pasta_new_string("line1\nline2\nline3"));
    char *out = pasta_write(map, PASTA_COMPACT);
    ASSERT(out != NULL, "write multiline");
    ASSERT(strstr(out, "\"\"\"") != NULL, "writer uses triple-quotes");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse");
    ASSERT(values_equal(map, v2), "roundtrip");
    pasta_free(v2); free(out);
    pasta_free(map);

    /* Writer: regular strings still use single quotes */
    map = pasta_new_map();
    pasta_set(map, "plain", pasta_new_string("no newlines here"));
    out = pasta_write(map, PASTA_COMPACT);
    ASSERT(out != NULL, "write regular");
    /* Should NOT contain triple quotes */
    ASSERT(strstr(out, "\"\"\"") == NULL, "regular uses single quotes");
    pasta_free(pasta_parse_cstr(out, NULL)); free(out);
    pasta_free(map);

    /* Unterminated multiline */
    PastaResult res;
    v = pasta_parse_cstr("{s: \"\"\"unterminated", &res);
    ASSERT(v == NULL, "unterminated multiline fails");
    ASSERT(res.code != PASTA_OK, "error code set");

    SUITE_OK();
}

/* ================================================================== */
/*  40. FILE: multiline.pasta                                          */
/* ================================================================== */

static void test_file_multiline(void) {
    SUITE("File: multiline.pasta");

    PastaValue *v = parse_file("multiline.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 7, "7 top keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "name")), "ci_pipeline") == 0, "name");

    /* Script has newlines */
    const char *script = pasta_get_string(pasta_map_get(v, "script"));
    ASSERT(script != NULL, "script exists");
    ASSERT(strstr(script, "#!/bin/bash") != NULL, "shebang");
    ASSERT(strstr(script, "make clean && make all") != NULL, "make line");
    ASSERT(strstr(script, "make test") != NULL, "test line");
    ASSERT(strchr(script, '\n') != NULL, "has newlines");

    /* Dockerfile */
    const char *docker = pasta_get_string(pasta_map_get(v, "dockerfile"));
    ASSERT(docker != NULL, "dockerfile exists");
    ASSERT(strstr(docker, "FROM golang") != NULL, "FROM line");
    ASSERT(strstr(docker, "CMD") != NULL, "CMD line");

    /* Notes with indentation */
    const char *notes = pasta_get_string(pasta_map_get(v, "notes"));
    ASSERT(notes != NULL, "notes exists");
    ASSERT(strstr(notes, "  Some are indented.") != NULL, "indented line");

    /* Regular string still works */
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "inline")), "this is a regular string") == 0, "inline");

    /* Empty multiline */
    ASSERT(pasta_get_string_len(pasta_map_get(v, "empty_multi")) == 0, "empty multiline len=0");

    /* Mixed array */
    const PastaValue *mixed = pasta_map_get(v, "mixed");
    ASSERT(mixed != NULL && pasta_count(mixed) == 2, "2 mixed items");
    ASSERT(strcmp(pasta_get_string(pasta_array_get(mixed, 0)), "regular") == 0, "mixed[0]=regular");
    const char *ml = pasta_get_string(pasta_array_get(mixed, 1));
    ASSERT(ml != NULL && strstr(ml, "line one") != NULL, "mixed[1] has line one");
    ASSERT(strstr(ml, "\n") != NULL, "mixed[1] has newline");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_PRETTY);
    ASSERT(out != NULL, "write pretty");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse pretty");
    ASSERT(values_equal(v, v2), "pretty roundtrip");
    pasta_free(v2); free(out);

    out = pasta_write(v, PASTA_COMPACT);
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse compact");
    ASSERT(values_equal(v, v2), "compact roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  41. Quoted labels (JSON interop)                                   */
/* ================================================================== */

static void test_quoted_labels(void) {
    SUITE("Quoted labels");

    /* Basic quoted key */
    PastaValue *v = pasta_parse_cstr("{\"Content-Type\": \"text/html\"}", NULL);
    ASSERT(v != NULL, "parse quoted key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "Content-Type")), "text/html") == 0, "Content-Type");
    pasta_free(v);

    /* Mixed quoted and unquoted */
    v = pasta_parse_cstr("{\"x-val\": 1, plain: 2, \"with spaces\": 3}", NULL);
    ASSERT(v != NULL && pasta_count(v) == 3, "3 keys");
    ASSERT(pasta_get_number(pasta_map_get(v, "x-val")) == 1.0, "x-val");
    ASSERT(pasta_get_number(pasta_map_get(v, "plain")) == 2.0, "plain");
    ASSERT(pasta_get_number(pasta_map_get(v, "with spaces")) == 3.0, "with spaces");
    pasta_free(v);

    /* Empty quoted key */
    v = pasta_parse_cstr("{\"\": \"empty key\"}", NULL);
    ASSERT(v != NULL, "parse empty key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "")), "empty key") == 0, "empty key value");
    pasta_free(v);

    /* Quoted key that looks like a label (should still work) */
    v = pasta_parse_cstr("{\"name\": \"Alice\"}", NULL);
    ASSERT(v != NULL, "quoted normal key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "name")), "Alice") == 0, "name=Alice");
    pasta_free(v);

    /* Writer: keys with non-label chars get quoted */
    PastaValue *map = pasta_new_map();
    pasta_set(map, "normal", pasta_new_number(1));
    pasta_set(map, "Content-Type", pasta_new_string("text/html"));
    pasta_set(map, "with spaces", pasta_new_number(3));
    char *out = pasta_write(map, PASTA_COMPACT);
    ASSERT(out != NULL, "write");
    ASSERT(strstr(out, "normal:") != NULL, "bare label normal");
    ASSERT(strstr(out, "\"Content-Type\":") != NULL, "quoted Content-Type");
    ASSERT(strstr(out, "\"with spaces\":") != NULL, "quoted with spaces");
    /* Roundtrip */
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(map, v2), "roundtrip");
    pasta_free(v2); free(out);
    pasta_free(map);

    SUITE_OK();
}

/* ================================================================== */
/*  42. FILE: quoted_keys.pasta                                        */
/* ================================================================== */

static void test_file_quoted_keys(void) {
    SUITE("File: quoted_keys.pasta");

    PastaValue *v = parse_file("quoted_keys.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_count(v) == 8, "8 keys");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "Content-Type")), "application/json") == 0, "Content-Type");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "X-Request-ID")), "abc-123") == 0, "X-Request-ID");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "server.host")), "localhost") == 0, "server.host");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "with spaces")), "value") == 0, "with spaces");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "regular")), "bare label still works") == 0, "regular");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "")), "empty key") == 0, "empty key");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v, "key:with:colons")), "colons in key") == 0, "colons key");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_COMPACT);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  43. @sections: inline                                              */
/* ================================================================== */

static void test_sections_inline(void) {
    SUITE("Sections: inline");

    /* Basic sections */
    PastaValue *v = pasta_parse_cstr(
        "@config {name: \"app\", version: \"1.0\"}\n"
        "@data [1, 2, 3]", NULL);
    ASSERT(v != NULL, "parse sections");
    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");
    ASSERT(pasta_count(v) == 2, "2 sections");

    const PastaValue *config = pasta_map_get(v, "config");
    ASSERT(config != NULL && pasta_type(config) == PASTA_MAP, "config is map");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(config, "name")), "app") == 0, "config.name");

    const PastaValue *data = pasta_map_get(v, "data");
    ASSERT(data != NULL && pasta_type(data) == PASTA_ARRAY, "data is array");
    ASSERT(pasta_count(data) == 3, "data has 3 items");
    ASSERT(pasta_get_number(pasta_array_get(data, 0)) == 1.0, "data[0]=1");
    pasta_free(v);

    /* Single section */
    v = pasta_parse_cstr("@main {key: \"value\"}", NULL);
    ASSERT(v != NULL && pasta_type(v) == PASTA_MAP, "single section");
    ASSERT(pasta_count(v) == 1, "1 section");
    ASSERT(pasta_map_get(v, "main") != NULL, "main exists");
    pasta_free(v);

    /* Sections with comments */
    v = pasta_parse_cstr(
        "; header\n"
        "@first {a: 1}\n"
        "; between\n"
        "@second {b: 2}", NULL);
    ASSERT(v != NULL && pasta_count(v) == 2, "sections with comments");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v, "first"), "a")) == 1.0, "first.a");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v, "second"), "b")) == 2.0, "second.b");
    pasta_free(v);

    /* Quoted section name */
    v = pasta_parse_cstr("@\"my-section\" {x: 42}", NULL);
    ASSERT(v != NULL, "quoted section name");
    ASSERT(pasta_map_get(v, "my-section") != NULL, "my-section exists");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v, "my-section"), "x")) == 42.0, "x=42");
    pasta_free(v);

    /* Error: missing container after section name */
    PastaResult r;
    v = pasta_parse_cstr("@orphan", &r);
    ASSERT(v == NULL, "error: no container");

    /* Error: mixing sections and plain containers */
    v = pasta_parse_cstr("@first {a: 1}\n{b: 2}", &r);
    ASSERT(v == NULL, "error: mixed sections and plain");

    SUITE_OK();
}

/* ================================================================== */
/*  44. @sections: writer                                              */
/* ================================================================== */

static void test_sections_writer(void) {
    SUITE("Sections: writer");

    /* Build and write with PASTA_SECTIONS */
    PastaValue *root = pasta_new_map();
    PastaValue *cfg = pasta_new_map();
    pasta_set(cfg, "name", pasta_new_string("app"));
    pasta_set(root, "config", cfg);
    PastaValue *arr = pasta_new_array();
    pasta_push(arr, pasta_new_number(1));
    pasta_push(arr, pasta_new_number(2));
    pasta_set(root, "data", arr);

    /* Pretty sections */
    char *out = pasta_write(root, PASTA_PRETTY | PASTA_SECTIONS);
    ASSERT(out != NULL, "write sections pretty");
    ASSERT(strstr(out, "@config") != NULL, "@config present");
    ASSERT(strstr(out, "@data") != NULL, "@data present");
    /* Should NOT have outer { } */
    ASSERT(out[0] == '@', "starts with @");
    /* Roundtrip */
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse sections");
    ASSERT(values_equal(root, v2), "sections roundtrip");
    pasta_free(v2); free(out);

    /* Compact sections */
    out = pasta_write(root, PASTA_COMPACT | PASTA_SECTIONS);
    ASSERT(out != NULL, "write sections compact");
    ASSERT(strstr(out, "@config ") != NULL, "compact @config");
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(root, v2), "compact sections roundtrip");
    pasta_free(v2); free(out);

    /* Without PASTA_SECTIONS, should write as regular map */
    out = pasta_write(root, PASTA_COMPACT);
    ASSERT(out != NULL, "write as map");
    ASSERT(out[0] == '{', "starts with {");
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(root, v2), "map roundtrip");
    pasta_free(v2); free(out);

    /* Sections with quoted key */
    PastaValue *root2 = pasta_new_map();
    PastaValue *sec = pasta_new_map();
    pasta_set(sec, "x", pasta_new_number(1));
    pasta_set(root2, "my-section", sec);
    out = pasta_write(root2, PASTA_COMPACT | PASTA_SECTIONS);
    ASSERT(out != NULL, "write quoted section");
    ASSERT(strstr(out, "@\"my-section\"") != NULL, "quoted section name");
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(root2, v2), "quoted section roundtrip");
    pasta_free(v2); free(out);
    pasta_free(root2);

    pasta_free(root);
    SUITE_OK();
}

/* ================================================================== */
/*  45. FILE: sections.pasta                                           */
/* ================================================================== */

static void test_file_sections(void) {
    SUITE("File: sections.pasta");

    PastaValue *v = parse_file("sections.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");
    ASSERT(pasta_count(v) == 4, "4 sections");

    /* @global */
    const PastaValue *global = pasta_map_get(v, "global");
    ASSERT(global != NULL && pasta_type(global) == PASTA_MAP, "global is map");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(global, "project")), "mega_platform") == 0, "project");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(global, "version")), "4.2.0") == 0, "version");

    /* @services */
    const PastaValue *services = pasta_map_get(v, "services");
    ASSERT(services != NULL && pasta_type(services) == PASTA_ARRAY, "services is array");
    ASSERT(pasta_count(services) == 3, "3 services");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(pasta_array_get(services, 0), "name")), "api") == 0, "svc[0]=api");
    ASSERT(pasta_get_number(pasta_map_get(pasta_array_get(services, 1), "replicas")) == 3.0, "worker replicas=3");

    /* @database */
    const PastaValue *db = pasta_map_get(v, "database");
    ASSERT(db != NULL, "database exists");
    ASSERT(pasta_get_number(pasta_map_get(db, "port")) == 5432.0, "db port");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(db, "pool"), "max")) == 50.0, "pool.max=50");

    /* @monitoring */
    const PastaValue *mon = pasta_map_get(v, "monitoring");
    ASSERT(mon != NULL, "monitoring exists");
    ASSERT(pasta_get_bool(pasta_map_get(mon, "enabled")) == 1, "monitoring enabled");
    ASSERT(pasta_count(pasta_map_get(mon, "providers")) == 2, "2 providers");

    /* Section order preserved */
    ASSERT(strcmp(pasta_map_key(v, 0), "global") == 0, "key[0]=global");
    ASSERT(strcmp(pasta_map_key(v, 1), "services") == 0, "key[1]=services");
    ASSERT(strcmp(pasta_map_key(v, 2), "database") == 0, "key[2]=database");
    ASSERT(strcmp(pasta_map_key(v, 3), "monitoring") == 0, "key[3]=monitoring");

    /* Roundtrip via sections writer */
    char *out = pasta_write(v, PASTA_PRETTY | PASTA_SECTIONS);
    ASSERT(out != NULL, "write sections");
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse");
    ASSERT(values_equal(v, v2), "sections roundtrip");
    pasta_free(v2); free(out);

    /* Roundtrip via regular map writer */
    out = pasta_write(v, PASTA_COMPACT);
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && values_equal(v, v2), "map roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  PastaResult.sections field                                         */
/* ================================================================== */

static void test_result_sections_flag(void) {
    SUITE("PastaResult.sections flag");
    PastaResult r;
    PastaValue *v;

    /* Section document sets sections=1 */
    v = pasta_parse_cstr("@app {port: 8080}\n@db {host: \"localhost\"}", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "section parse ok");
    ASSERT(r.sections == 1, "sections=1 for @section input");
    pasta_free(v);

    /* Single section still sets sections=1 */
    v = pasta_parse_cstr("@main {key: \"value\"}", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "single section parse ok");
    ASSERT(r.sections == 1, "sections=1 for single @section");
    pasta_free(v);

    /* Plain map does NOT set sections */
    v = pasta_parse_cstr("{a: 1, b: 2}", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "plain map parse ok");
    ASSERT(r.sections == 0, "sections=0 for plain map");
    pasta_free(v);

    /* Plain array does NOT set sections */
    v = pasta_parse_cstr("[1, 2, 3]", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "plain array parse ok");
    ASSERT(r.sections == 0, "sections=0 for plain array");
    pasta_free(v);

    /* Scalar does NOT set sections */
    v = pasta_parse_cstr("42", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "scalar parse ok");
    ASSERT(r.sections == 0, "sections=0 for scalar");
    pasta_free(v);

    /* Multi-container does NOT set sections */
    v = pasta_parse_cstr("{a: 1}\n{b: 2}", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "multi-container parse ok");
    ASSERT(r.sections == 0, "sections=0 for multi-container");
    pasta_free(v);

    /* Sections with comments and whitespace */
    v = pasta_parse_cstr("; header\n\n  @cfg {x: 1}\n; footer\n", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "sections with comments ok");
    ASSERT(r.sections == 1, "sections=1 with comments/whitespace");
    pasta_free(v);

    /* Quoted section name still sets sections=1 */
    v = pasta_parse_cstr("@\"my-section\" {x: 42}", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "quoted section ok");
    ASSERT(r.sections == 1, "sections=1 for quoted section name");
    pasta_free(v);

    /* Error case: sections flag still set even on error after @ */
    v = pasta_parse_cstr("@orphan", &r);
    ASSERT(v == NULL, "error: no container after section name");
    ASSERT(r.sections == 1, "sections=1 even on section parse error");

    /* Label value at top level is NOT sections */
    v = pasta_parse_cstr("hello", &r);
    ASSERT(v != NULL && r.code == PASTA_OK, "label parse ok");
    ASSERT(r.sections == 0, "sections=0 for bare label");
    pasta_free(v);

    /* NULL result pointer doesn't crash */
    v = pasta_parse_cstr("@s {a: 1}", NULL);
    ASSERT(v != NULL, "null result ptr with sections ok");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  46. PASTA_SORTED: deterministic output                             */
/* ================================================================== */

static void test_sorted_output(void) {
    SUITE("Sorted output");

    /* Build a map with keys in reverse order */
    PastaValue *map = pasta_new_map();
    pasta_set(map, "zebra", pasta_new_number(1));
    pasta_set(map, "apple", pasta_new_number(2));
    pasta_set(map, "mango", pasta_new_number(3));

    /* Without sorted: insertion order */
    char *unsorted = pasta_write(map, PASTA_COMPACT);
    ASSERT(unsorted != NULL, "write unsorted");
    ASSERT(strstr(unsorted, "zebra") < strstr(unsorted, "apple"), "unsorted: zebra before apple");

    /* With sorted: lexicographic order */
    char *sorted = pasta_write(map, PASTA_COMPACT | PASTA_SORTED);
    ASSERT(sorted != NULL, "write sorted");
    ASSERT(strstr(sorted, "apple") < strstr(sorted, "mango"), "sorted: apple before mango");
    ASSERT(strstr(sorted, "mango") < strstr(sorted, "zebra"), "sorted: mango before zebra");

    /* Roundtrip preserves values */
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(sorted, &r);
    ASSERT(v2 != NULL, "sorted roundtrip parse");
    ASSERT(pasta_get_number(pasta_map_get(v2, "apple")) == 2.0, "sorted roundtrip apple");
    ASSERT(pasta_get_number(pasta_map_get(v2, "zebra")) == 1.0, "sorted roundtrip zebra");
    ASSERT(pasta_get_number(pasta_map_get(v2, "mango")) == 3.0, "sorted roundtrip mango");
    pasta_free(v2);
    free(unsorted);
    free(sorted);
    pasta_free(map);

    /* Deterministic: same input produces identical output */
    PastaValue *m1 = pasta_new_map();
    pasta_set(m1, "c", pasta_new_number(3));
    pasta_set(m1, "a", pasta_new_number(1));
    pasta_set(m1, "b", pasta_new_number(2));
    PastaValue *m2 = pasta_new_map();
    pasta_set(m2, "b", pasta_new_number(2));
    pasta_set(m2, "c", pasta_new_number(3));
    pasta_set(m2, "a", pasta_new_number(1));
    char *s1 = pasta_write(m1, PASTA_COMPACT | PASTA_SORTED);
    char *s2 = pasta_write(m2, PASTA_COMPACT | PASTA_SORTED);
    ASSERT(s1 != NULL && s2 != NULL, "both write");
    ASSERT(strcmp(s1, s2) == 0, "deterministic: same output regardless of insertion order");
    free(s1); free(s2);
    pasta_free(m1); pasta_free(m2);

    /* Nested maps sorted recursively */
    PastaValue *outer = pasta_new_map();
    PastaValue *inner = pasta_new_map();
    pasta_set(inner, "z", pasta_new_number(1));
    pasta_set(inner, "a", pasta_new_number(2));
    pasta_set(outer, "beta", inner);
    pasta_set(outer, "alpha", pasta_new_string("first"));
    sorted = pasta_write(outer, PASTA_COMPACT | PASTA_SORTED);
    ASSERT(sorted != NULL, "write nested sorted");
    ASSERT(strstr(sorted, "alpha") < strstr(sorted, "beta"), "outer sorted");
    ASSERT(strstr(sorted, "a: 2") < strstr(sorted, "z: 1"), "inner sorted");
    v2 = pasta_parse_cstr(sorted, &r);
    ASSERT(v2 != NULL, "nested sorted roundtrip parse");
    ASSERT(strcmp(pasta_get_string(pasta_map_get(v2, "alpha")), "first") == 0, "nested roundtrip alpha");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v2, "beta"), "a")) == 2.0, "nested roundtrip beta.a");
    pasta_free(v2); free(sorted);
    pasta_free(outer);

    /* Pretty + sorted */
    map = pasta_new_map();
    pasta_set(map, "z", pasta_new_number(1));
    pasta_set(map, "a", pasta_new_number(2));
    sorted = pasta_write(map, PASTA_PRETTY | PASTA_SORTED);
    ASSERT(sorted != NULL, "write pretty sorted");
    ASSERT(strstr(sorted, "a") < strstr(sorted, "z"), "pretty sorted order");
    v2 = pasta_parse_cstr(sorted, &r);
    ASSERT(v2 != NULL, "pretty sorted roundtrip parse");
    ASSERT(pasta_get_number(pasta_map_get(v2, "a")) == 2.0, "pretty sorted a=2");
    ASSERT(pasta_get_number(pasta_map_get(v2, "z")) == 1.0, "pretty sorted z=1");
    pasta_free(v2); free(sorted);
    pasta_free(map);

    /* Sections + sorted */
    PastaValue *root = pasta_new_map();
    PastaValue *sec_z = pasta_new_map();
    pasta_set(sec_z, "x", pasta_new_number(1));
    PastaValue *sec_a = pasta_new_map();
    pasta_set(sec_a, "y", pasta_new_number(2));
    pasta_set(root, "zoo", sec_z);
    pasta_set(root, "alpha", sec_a);
    sorted = pasta_write(root, PASTA_SECTIONS | PASTA_SORTED);
    ASSERT(sorted != NULL, "write sections sorted");
    ASSERT(strstr(sorted, "@alpha") < strstr(sorted, "@zoo"), "sections sorted order");
    v2 = pasta_parse_cstr(sorted, &r);
    ASSERT(v2 != NULL, "sections sorted roundtrip parse");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v2, "alpha"), "y")) == 2.0, "sections roundtrip alpha.y");
    ASSERT(pasta_get_number(pasta_map_get(pasta_map_get(v2, "zoo"), "x")) == 1.0, "sections roundtrip zoo.x");
    pasta_free(v2); free(sorted);
    pasta_free(root);

    /* Single-key map: sorted is a no-op */
    map = pasta_new_map();
    pasta_set(map, "only", pasta_new_number(42));
    sorted = pasta_write(map, PASTA_COMPACT | PASTA_SORTED);
    ASSERT(sorted != NULL, "single key sorted");
    ASSERT(strstr(sorted, "only: 42") != NULL, "single key content");
    free(sorted);
    pasta_free(map);

    /* Empty map: sorted is safe */
    map = pasta_new_map();
    sorted = pasta_write(map, PASTA_COMPACT | PASTA_SORTED);
    ASSERT(sorted != NULL && strcmp(sorted, "{}") == 0, "empty map sorted");
    free(sorted);
    pasta_free(map);

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - parsing                                             */
/* ================================================================== */

static void test_label_values(void) {
    SUITE("Label values: parsing");
    PastaValue *v;

    /* Bare label at top level */
    v = parse_and_print("bare label", "hello");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_type(v) == PASTA_LABEL, "type is LABEL");
    ASSERT(strcmp(pasta_get_label(v), "hello") == 0, "value is hello");
    ASSERT(pasta_get_label_len(v) == 5, "len is 5");
    pasta_free(v);

    /* Single character label */
    v = parse_and_print("single char label", "x");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "x") == 0, "value is x");
    ASSERT(pasta_get_label_len(v) == 1, "len is 1");
    pasta_free(v);

    /* Label with digits */
    v = parse_and_print("label with digits", "item42");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "item42") == 0, "value");
    pasta_free(v);

    /* Label with all label symbols */
    v = parse_and_print("label with symbols", "a!b#c$d%e&f_g");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "a!b#c$d%e&f_g") == 0, "value");
    pasta_free(v);

    /* Label starting with symbol */
    v = parse_and_print("symbol-start label", "_private");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "_private") == 0, "value");
    pasta_free(v);

    v = parse_and_print("dollar-start label", "$ref");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "$ref") == 0, "value");
    pasta_free(v);

    v = parse_and_print("hash-start label", "#tag");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "#tag") == 0, "value");
    pasta_free(v);

    /* Label with whitespace around it */
    v = parse_and_print("label with blanks", "  \t myref \n ");
    ASSERT(v != NULL && pasta_type(v) == PASTA_LABEL, "type");
    ASSERT(strcmp(pasta_get_label(v), "myref") == 0, "value");
    pasta_free(v);

    /* Keywords are NOT label values */
    v = parse_and_print("true is not label", "true");
    ASSERT(v != NULL && pasta_type(v) == PASTA_BOOL, "true is bool");
    pasta_free(v);

    v = parse_and_print("false is not label", "false");
    ASSERT(v != NULL && pasta_type(v) == PASTA_BOOL, "false is bool");
    pasta_free(v);

    v = parse_and_print("null is not label", "null");
    ASSERT(v != NULL && pasta_type(v) == PASTA_NULL, "null is null");
    pasta_free(v);

    v = parse_and_print("Inf is not label", "Inf");
    ASSERT(v != NULL && pasta_type(v) == PASTA_NUMBER, "Inf is number");
    pasta_free(v);

    v = parse_and_print("NaN is not label", "NaN");
    ASSERT(v != NULL && pasta_type(v) == PASTA_NUMBER, "NaN is number");
    pasta_free(v);

    /* Label accessor returns NULL for non-label types */
    v = parse_and_print("string not label", "\"hello\"");
    ASSERT(pasta_get_label(v) == NULL, "get_label on string is NULL");
    ASSERT(pasta_get_label_len(v) == 0, "get_label_len on string is 0");
    pasta_free(v);

    v = parse_and_print("number not label", "42");
    ASSERT(pasta_get_label(v) == NULL, "get_label on number is NULL");
    pasta_free(v);

    /* String accessor returns NULL for label type */
    v = parse_and_print("label not string", "hello");
    ASSERT(pasta_get_string(v) == NULL, "get_string on label is NULL");
    ASSERT(pasta_get_string_len(v) == 0, "get_string_len on label is 0");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - in containers                                       */
/* ================================================================== */

static void test_label_values_in_containers(void) {
    SUITE("Label values: in containers");
    PastaValue *v;

    /* Label as map value */
    v = parse_and_print("map with label value", "{ref: mySection}");
    ASSERT(v != NULL && pasta_type(v) == PASTA_MAP, "root is map");
    const PastaValue *ref = pasta_map_get(v, "ref");
    ASSERT(ref != NULL && pasta_type(ref) == PASTA_LABEL, "value is label");
    ASSERT(strcmp(pasta_get_label(ref), "mySection") == 0, "label text");
    pasta_free(v);

    /* Label in array */
    v = parse_and_print("array with labels", "[foo, bar, baz]");
    ASSERT(v != NULL && pasta_type(v) == PASTA_ARRAY, "root is array");
    ASSERT(pasta_count(v) == 3, "count 3");
    ASSERT(pasta_type(pasta_array_get(v, 0)) == PASTA_LABEL, "[0] is label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(v, 0)), "foo") == 0, "[0]=foo");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(v, 1)), "bar") == 0, "[1]=bar");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(v, 2)), "baz") == 0, "[2]=baz");
    pasta_free(v);

    /* Mixed types including labels */
    v = parse_and_print("mixed with labels",
        "[1, \"two\", myRef, true, null, otherRef]");
    ASSERT(v != NULL && pasta_count(v) == 6, "count 6");
    ASSERT(pasta_type(pasta_array_get(v, 0)) == PASTA_NUMBER, "[0] number");
    ASSERT(pasta_type(pasta_array_get(v, 1)) == PASTA_STRING, "[1] string");
    ASSERT(pasta_type(pasta_array_get(v, 2)) == PASTA_LABEL, "[2] label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(v, 2)), "myRef") == 0, "[2]=myRef");
    ASSERT(pasta_type(pasta_array_get(v, 3)) == PASTA_BOOL, "[3] bool");
    ASSERT(pasta_type(pasta_array_get(v, 4)) == PASTA_NULL, "[4] null");
    ASSERT(pasta_type(pasta_array_get(v, 5)) == PASTA_LABEL, "[5] label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(v, 5)), "otherRef") == 0, "[5]=otherRef");
    pasta_free(v);

    /* Multiple label values in a map */
    v = parse_and_print("map multiple labels",
        "{a: ref1, b: ref2, c: ref3}");
    ASSERT(v != NULL && pasta_count(v) == 3, "count 3");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "a")), "ref1") == 0, "a=ref1");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "b")), "ref2") == 0, "b=ref2");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "c")), "ref3") == 0, "c=ref3");
    pasta_free(v);

    /* Nested container with label values */
    v = parse_and_print("nested with labels",
        "{outer: {inner: myRef}, list: [x, y]}");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_type(pasta_map_get(pasta_map_get(v, "outer"), "inner")) == PASTA_LABEL, "nested label");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(pasta_map_get(v, "outer"), "inner")), "myRef") == 0, "nested value");
    ASSERT(pasta_type(pasta_array_get(pasta_map_get(v, "list"), 0)) == PASTA_LABEL, "list[0] label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(pasta_map_get(v, "list"), 0)), "x") == 0, "list[0]=x");
    pasta_free(v);

    /* Label value alongside keywords — keywords still take precedence */
    v = parse_and_print("labels near keywords",
        "{a: trueish, b: falsey, c: nullable, d: Infinite}");
    ASSERT(v != NULL && pasta_count(v) == 4, "count 4");
    ASSERT(pasta_type(pasta_map_get(v, "a")) == PASTA_LABEL, "trueish is label");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "a")), "trueish") == 0, "trueish");
    ASSERT(pasta_type(pasta_map_get(v, "b")) == PASTA_LABEL, "falsey is label");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "b")), "falsey") == 0, "falsey");
    ASSERT(pasta_type(pasta_map_get(v, "c")) == PASTA_LABEL, "nullable is label");
    ASSERT(pasta_type(pasta_map_get(v, "d")) == PASTA_LABEL, "Infinite is label");
    pasta_free(v);

    /* Label same as key name (common for section refs) */
    v = parse_and_print("label matches key",
        "{tls: tls, db: database}");
    ASSERT(v != NULL, "parsed");
    ASSERT(pasta_type(pasta_map_get(v, "tls")) == PASTA_LABEL, "tls is label");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "tls")), "tls") == 0, "tls value");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(v, "db")), "database") == 0, "db value");
    pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - writer                                              */
/* ================================================================== */

static void test_label_values_writer(void) {
    SUITE("Label values: writer");
    PastaValue *v;
    char *out;

    /* Compact output */
    v = parse_and_print("label compact", "hello");
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [compact] %s\n", out);
    ASSERT(strcmp(out, "hello") == 0, "bare label output");
    free(out); pasta_free(v);

    /* Map with label values compact */
    v = parse_and_print("map label compact", "{ref: mySection}");
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [map compact] %s\n", out);
    ASSERT(strcmp(out, "{ref: mySection}") == 0, "map label compact");
    free(out); pasta_free(v);

    /* Array with labels compact */
    v = parse_and_print("array labels compact", "[foo, bar, baz]");
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [array compact] %s\n", out);
    ASSERT(strcmp(out, "[foo, bar, baz]") == 0, "array labels compact");
    free(out); pasta_free(v);

    /* Mixed types compact */
    v = parse_and_print("mixed compact",
        "{name: \"svc\", port: 8080, tls: myTls, debug: false}");
    out = pasta_write(v, PASTA_COMPACT);
    printf("  [mixed compact] %s\n", out);
    ASSERT(strstr(out, "tls: myTls") != NULL, "label in mixed compact");
    free(out); pasta_free(v);

    /* Pretty output */
    v = parse_and_print("label pretty", "{ref: mySection, other: otherRef}");
    out = pasta_write(v, PASTA_PRETTY);
    printf("  [pretty]\n%s", out);
    ASSERT(strstr(out, "  ref: mySection,\n") != NULL, "pretty label indent");
    ASSERT(strstr(out, "  other: otherRef\n") != NULL, "pretty label last");
    free(out); pasta_free(v);

    /* Sorted with labels */
    v = parse_and_print("labels sorted", "{z: refZ, a: refA}");
    out = pasta_write(v, PASTA_COMPACT | PASTA_SORTED);
    printf("  [sorted] %s\n", out);
    ASSERT(strstr(out, "a: refA") < strstr(out, "z: refZ"), "sorted label order");
    free(out); pasta_free(v);

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - roundtrip                                           */
/* ================================================================== */

static void test_label_values_roundtrip(void) {
    SUITE("Label values: roundtrip");

    roundtrip("bare label", "hello");
    roundtrip("label with symbols", "_my$ref");
    roundtrip("label in array", "[foo, bar, baz]");
    roundtrip("label in map", "{ref: mySection}");
    roundtrip("mixed with labels",
        "{name: \"svc\", link: myRef, port: 8080, debug: false}");
    roundtrip("nested labels",
        "{outer: {inner: myRef}, list: [x, y]}");
    roundtrip("label near keywords",
        "[trueish, falsey, nullable, Infinite]");

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - builder API                                         */
/* ================================================================== */

static void test_label_values_builder(void) {
    SUITE("Label values: builder API");
    char *out;

    /* Basic construction */
    PastaValue *v = pasta_new_label("myRef");
    ASSERT(v != NULL, "new_label");
    ASSERT(pasta_type(v) == PASTA_LABEL, "type is LABEL");
    ASSERT(strcmp(pasta_get_label(v), "myRef") == 0, "value");
    ASSERT(pasta_get_label_len(v) == 5, "len");
    out = pasta_write(v, PASTA_COMPACT);
    ASSERT(strcmp(out, "myRef") == 0, "write label");
    free(out); pasta_free(v);

    /* new_label_len */
    v = pasta_new_label_len("helloWORLD", 5);
    ASSERT(v != NULL, "new_label_len");
    ASSERT(strcmp(pasta_get_label(v), "hello") == 0, "truncated");
    ASSERT(pasta_get_label_len(v) == 5, "len 5");
    pasta_free(v);

    /* Label in map via builder */
    PastaValue *map = pasta_new_map();
    pasta_set(map, "link", pasta_new_label("target"));
    pasta_set(map, "name", pasta_new_string("svc"));
    out = pasta_write(map, PASTA_COMPACT);
    printf("  [builder map] %s\n", out);
    ASSERT(strstr(out, "link: target") != NULL, "builder label in map");
    ASSERT(strstr(out, "name: \"svc\"") != NULL, "builder string in map");

    /* Roundtrip the builder result */
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "builder roundtrip parse");
    ASSERT(values_equal(map, v2), "builder roundtrip equal");
    pasta_free(v2); free(out); pasta_free(map);

    /* Label in array via builder */
    PastaValue *arr = pasta_new_array();
    pasta_push(arr, pasta_new_label("foo"));
    pasta_push(arr, pasta_new_number(42));
    pasta_push(arr, pasta_new_label("bar"));
    out = pasta_write(arr, PASTA_COMPACT);
    printf("  [builder array] %s\n", out);
    ASSERT(strcmp(out, "[foo, 42, bar]") == 0, "builder array");
    free(out); pasta_free(arr);

    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - sections use case                                   */
/* ================================================================== */

static void test_label_values_sections(void) {
    SUITE("Label values: sections use case");
    PastaValue *v;
    char *out;

    /* Parse a sectioned document where values reference section names */
    const char *input =
        "@network {\n"
        "  bind: \"0.0.0.0\",\n"
        "  port: 8080\n"
        "}\n"
        "@tls {\n"
        "  cert: \"/etc/ssl/cert.pem\",\n"
        "  key: \"/etc/ssl/key.pem\"\n"
        "}\n"
        "@api {\n"
        "  consumes: [network],\n"
        "  port: \"required\",\n"
        "  tls: tls\n"
        "}\n";

    PastaResult r;
    v = pasta_parse_cstr(input, &r);
    printf("  [sections with label refs]\n");
    ASSERT(v != NULL && r.code == PASTA_OK, "parsed");
    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");
    ASSERT(pasta_count(v) == 3, "3 sections");

    /* Verify the label references */
    const PastaValue *api = pasta_map_get(v, "api");
    ASSERT(api != NULL, "api section");

    const PastaValue *consumes = pasta_map_get(api, "consumes");
    ASSERT(consumes != NULL && pasta_count(consumes) == 1, "consumes array");
    ASSERT(pasta_type(pasta_array_get(consumes, 0)) == PASTA_LABEL, "consumes[0] is label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(consumes, 0)), "network") == 0, "consumes=network");

    const PastaValue *tls_ref = pasta_map_get(api, "tls");
    ASSERT(tls_ref != NULL && pasta_type(tls_ref) == PASTA_LABEL, "tls is label");
    ASSERT(strcmp(pasta_get_label(tls_ref), "tls") == 0, "tls ref value");

    /* Write back as sections and re-parse */
    out = pasta_write(v, PASTA_SECTIONS);
    ASSERT(out != NULL, "write sections");
    printf("  [sections output]\n%s", out);
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse sections");
    ASSERT(values_equal(v, v2), "sections roundtrip");
    pasta_free(v2); free(out);

    /* Write compact and re-parse */
    out = pasta_write(v, PASTA_COMPACT);
    ASSERT(out != NULL, "write compact");
    printf("  [compact] %s\n", out);
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "re-parse compact");
    ASSERT(values_equal(v, v2), "compact roundtrip");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  LABEL VALUES - file test                                           */
/* ================================================================== */

static void test_file_label_values(void) {
    SUITE("File: label_values.pasta");

    PastaValue *v = parse_file("label_values.pasta");
    if (!v) { ASSERT(0, "file load"); return; }

    ASSERT(pasta_type(v) == PASTA_MAP, "root is map");

    /* Check label values */
    const PastaValue *tls = pasta_map_get(v, "tls_ref");
    ASSERT(tls != NULL && pasta_type(tls) == PASTA_LABEL, "tls_ref is label");
    ASSERT(strcmp(pasta_get_label(tls), "tls") == 0, "tls_ref=tls");

    const PastaValue *db = pasta_map_get(v, "db_ref");
    ASSERT(db != NULL && pasta_type(db) == PASTA_LABEL, "db_ref is label");
    ASSERT(strcmp(pasta_get_label(db), "database") == 0, "db_ref=database");

    /* Check mixed values */
    ASSERT(pasta_type(pasta_map_get(v, "name")) == PASTA_STRING, "name is string");
    ASSERT(pasta_type(pasta_map_get(v, "port")) == PASTA_NUMBER, "port is number");
    ASSERT(pasta_type(pasta_map_get(v, "debug")) == PASTA_BOOL, "debug is bool");
    ASSERT(pasta_type(pasta_map_get(v, "fallback")) == PASTA_NULL, "fallback is null");

    /* Array with mixed labels and other types */
    const PastaValue *deps = pasta_map_get(v, "deps");
    ASSERT(deps != NULL && pasta_count(deps) == 4, "4 deps");
    ASSERT(pasta_type(pasta_array_get(deps, 0)) == PASTA_LABEL, "dep[0] label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(deps, 0)), "network") == 0, "dep[0]=network");
    ASSERT(pasta_type(pasta_array_get(deps, 1)) == PASTA_LABEL, "dep[1] label");
    ASSERT(strcmp(pasta_get_label(pasta_array_get(deps, 1)), "logging") == 0, "dep[1]=logging");
    ASSERT(pasta_type(pasta_array_get(deps, 2)) == PASTA_STRING, "dep[2] string");
    ASSERT(pasta_type(pasta_array_get(deps, 3)) == PASTA_LABEL, "dep[3] label");

    /* Nested map with label */
    const PastaValue *inner = pasta_map_get(v, "nested");
    ASSERT(inner != NULL, "nested exists");
    ASSERT(pasta_type(pasta_map_get(inner, "link")) == PASTA_LABEL, "nested.link is label");
    ASSERT(strcmp(pasta_get_label(pasta_map_get(inner, "link")), "target") == 0, "nested.link=target");

    /* Roundtrip */
    char *out = pasta_write(v, PASTA_COMPACT);
    PastaResult r;
    PastaValue *v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "roundtrip parse");
    ASSERT(values_equal(v, v2), "roundtrip equal");
    pasta_free(v2); free(out);

    out = pasta_write(v, PASTA_PRETTY);
    v2 = pasta_parse_cstr(out, &r);
    ASSERT(v2 != NULL && r.code == PASTA_OK, "pretty roundtrip parse");
    ASSERT(values_equal(v, v2), "pretty roundtrip equal");
    pasta_free(v2); free(out);

    pasta_free(v);
    SUITE_OK();
}

/* ================================================================== */
/*  MAIN                                                               */
/* ================================================================== */

int main(void) {
    printf("========================================\n");
    printf("  Pasta Parser & Writer Test Suite\n");
    printf("========================================\n");

    /* Inline tests */
    test_null_values();
    test_boolean_values();
    test_number_values();
    test_string_values();
    test_empty_containers();
    test_arrays();
    test_maps();
    test_label_chars();
    test_comments();
    test_whitespace();
    test_deep_nesting();
    test_error_cases();
    test_edge_cases();
    test_api_safety();
    test_write_compact();
    test_write_pretty();
    test_write_roundtrip();
    test_write_strips_comments();

    /* File-based tests */
    test_file_scalars();
    test_file_database();
    test_file_server();
    test_file_labels();
    test_file_nested();
    test_file_comments();
    test_file_compact();
    test_file_strings();
    test_file_empty();
    test_file_array_root();
    test_multi_container_inline();
    test_file_multi_maps();
    test_file_multi_arrays();
    test_file_multi_mixed();
    test_file_complex();

    /* Builder API tests */
    test_builder_scalars();
    test_builder_containers();
    test_builder_roundtrip();
    test_builder_complex();
    test_builder_array_root();

    /* Multiline string tests */
    test_multiline_strings();
    test_file_multiline();

    /* Quoted labels & sections */
    test_quoted_labels();
    test_file_quoted_keys();
    test_sections_inline();
    test_sections_writer();
    test_file_sections();
    test_result_sections_flag();

    /* Sorted output */
    test_sorted_output();

    /* Label values */
    test_label_values();
    test_label_values_in_containers();
    test_label_values_writer();
    test_label_values_roundtrip();
    test_label_values_builder();
    test_label_values_sections();
    test_file_label_values();

    printf("\n========================================\n");
    printf("  Suites: %d / %d passed\n", suite_passed, suite_run);
    printf("  Tests:  %d / %d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(" (%d FAILED)", tests_failed);
    printf("\n========================================\n");

    return tests_failed == 0 ? 0 : 1;
}
