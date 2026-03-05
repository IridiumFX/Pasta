#include "pasta_internal.h"
#include <stdio.h>

typedef struct {
    Lexer       lex;
    Token       current;
    PastaResult result;
    int         had_error;
} Parser;

static void parser_init(Parser *p, const char *src, size_t len) {
    lexer_init(&p->lex, src, len);
    p->had_error    = 0;
    p->result.code  = PASTA_OK;
    p->result.line  = 0;
    p->result.col   = 0;
    p->result.message[0] = '\0';
    p->current = lexer_next(&p->lex);
}

static void parser_error_code(Parser *p, PastaError code, const char *msg) {
    if (p->had_error) return;
    p->had_error    = 1;
    p->result.code  = code;
    p->result.line  = p->current.line;
    p->result.col   = p->current.col;
    snprintf(p->result.message, sizeof(p->result.message), "%s", msg);
}

static void parser_error(Parser *p, const char *msg) {
    parser_error_code(p, PASTA_ERR_SYNTAX, msg);
}

static void advance(Parser *p) {
    p->current = lexer_next(&p->lex);
    if (p->current.type == TOK_ERROR) {
        parser_error(p, p->current.start);
    }
}

static int check(const Parser *p, TokenType type) {
    return p->current.type == type;
}

static int expect(Parser *p, TokenType type, const char *msg) {
    if (check(p, type)) {
        advance(p);
        return 1;
    }
    parser_error(p, msg);
    return 0;
}

/* Is this token a valid map key? (label or quoted string) */
static int is_key_token(const Parser *p) {
    return check(p, TOK_LABEL) || check(p, TOK_STRING);
}

/* Forward declaration */
static PastaValue *parse_value(Parser *p);

static double parse_number_literal(const char *start, size_t len) {
    if (len == 3 && memcmp(start, "Inf", 3) == 0)   return INFINITY;
    if (len == 4 && memcmp(start, "-Inf", 4) == 0)   return -INFINITY;
    if (len == 3 && memcmp(start, "NaN", 3) == 0)    return NAN;

    char buf[64];
    size_t n = len < sizeof(buf) - 1 ? len : sizeof(buf) - 1;
    memcpy(buf, start, n);
    buf[n] = '\0';
    return strtod(buf, NULL);
}

static PastaValue *extract_string(const char *start, size_t len) {
    /* start includes surrounding quotes, strip them */
    return pasta_value_string(start + 1, len - 2);
}

static PastaValue *extract_mstring(const char *start, size_t len) {
    /* start includes surrounding """, strip 3 from each side */
    return pasta_value_string(start + 3, len - 6);
}

/* Extract key text from a label or quoted-string token */
static void extract_key(const Token *tok, const char **key, size_t *key_len) {
    if (tok->type == TOK_STRING) {
        /* Strip surrounding quotes */
        *key = tok->start + 1;
        *key_len = tok->len - 2;
    } else {
        *key = tok->start;
        *key_len = tok->len;
    }
}

static PastaValue *parse_array(Parser *p) {
    advance(p); /* consume [ */
    PastaValue *arr = pasta_value_array();
    if (!arr) { parser_error(p, "allocation failed"); return NULL; }

    if (!check(p, TOK_RBRACKET)) {
        PastaValue *elem = parse_value(p);
        if (!elem || p->had_error) { pasta_free(arr); pasta_free(elem); return NULL; }
        pasta_array_push(arr, elem);

        while (check(p, TOK_COMMA)) {
            advance(p);
            elem = parse_value(p);
            if (!elem || p->had_error) { pasta_free(arr); pasta_free(elem); return NULL; }
            pasta_array_push(arr, elem);
        }
    }

    if (!expect(p, TOK_RBRACKET, "expected ']'")) { pasta_free(arr); return NULL; }
    return arr;
}

static PastaValue *parse_map(Parser *p) {
    advance(p); /* consume { */
    PastaValue *map = pasta_value_map();
    if (!map) { parser_error(p, "allocation failed"); return NULL; }

    if (!check(p, TOK_RBRACE)) {
        /* member: label-or-string : value */
        if (!is_key_token(p)) {
            parser_error(p, "expected label or quoted key in map");
            pasta_free(map); return NULL;
        }
        Token key_tok = p->current;
        advance(p);
        if (!expect(p, TOK_COLON, "expected ':' after key")) { pasta_free(map); return NULL; }
        PastaValue *val = parse_value(p);
        if (!val || p->had_error) { pasta_free(map); pasta_free(val); return NULL; }
        const char *k; size_t klen;
        extract_key(&key_tok, &k, &klen);
        pasta_map_put(map, k, klen, val);

        while (check(p, TOK_COMMA)) {
            advance(p);
            if (!is_key_token(p)) {
                parser_error(p, "expected label or quoted key in map");
                pasta_free(map); return NULL;
            }
            key_tok = p->current;
            advance(p);
            if (!expect(p, TOK_COLON, "expected ':' after key")) { pasta_free(map); return NULL; }
            val = parse_value(p);
            if (!val || p->had_error) { pasta_free(map); pasta_free(val); return NULL; }
            extract_key(&key_tok, &k, &klen);
            pasta_map_put(map, k, klen, val);
        }
    }

    if (!expect(p, TOK_RBRACE, "expected '}'")) { pasta_free(map); return NULL; }
    return map;
}

static PastaValue *parse_value(Parser *p) {
    if (p->had_error) return NULL;

    switch (p->current.type) {
        case TOK_LBRACE:   return parse_map(p);
        case TOK_LBRACKET: return parse_array(p);
        case TOK_STRING: {
            PastaValue *v = extract_string(p->current.start, p->current.len);
            if (!v) parser_error(p, "allocation failed");
            advance(p);
            return v;
        }
        case TOK_MSTRING: {
            PastaValue *v = extract_mstring(p->current.start, p->current.len);
            if (!v) parser_error(p, "allocation failed");
            advance(p);
            return v;
        }
        case TOK_NUMBER: {
            double n = parse_number_literal(p->current.start, p->current.len);
            PastaValue *v = pasta_value_number(n);
            if (!v) parser_error(p, "allocation failed");
            advance(p);
            return v;
        }
        case TOK_TRUE:  advance(p); return pasta_value_bool(1);
        case TOK_FALSE: advance(p); return pasta_value_bool(0);
        case TOK_NULL:  advance(p); return pasta_value_null();
        case TOK_EOF:
            parser_error_code(p, PASTA_ERR_UNEXPECTED_EOF, "unexpected end of input");
            return NULL;
        default:
            parser_error_code(p, PASTA_ERR_UNEXPECTED_TOKEN, "unexpected token");
            return NULL;
    }
}

/* Check if the current token starts a container */
static int is_container_start(const Parser *p) {
    return check(p, TOK_LBRACE) || check(p, TOK_LBRACKET);
}

/* Extract section name from a label or quoted-string token */
static void extract_section_name(const Token *tok, const char **name, size_t *len) {
    if (tok->type == TOK_STRING) {
        *name = tok->start + 1;
        *len = tok->len - 2;
    } else {
        *name = tok->start;
        *len = tok->len;
    }
}

/* ---- Public parse API ---- */

PASTA_API PastaValue *pasta_parse(const char *input, size_t len, PastaResult *result) {
    Parser p;
    parser_init(&p, input, len);

    /* Check for @section syntax */
    if (check(&p, TOK_AT)) {
        /* Section mode: @name container, @name container, ... → map */
        PastaValue *map = pasta_value_map();
        if (!map) { parser_error(&p, "allocation failed"); if (result) *result = p.result; return NULL; }

        while (!p.had_error && check(&p, TOK_AT)) {
            advance(&p); /* consume @ */

            /* Section name: label or quoted string */
            if (!check(&p, TOK_LABEL) && !check(&p, TOK_STRING)) {
                parser_error(&p, "expected section name after '@'");
                pasta_free(map);
                if (result) *result = p.result;
                return NULL;
            }
            Token name_tok = p.current;
            advance(&p);

            /* Container must follow */
            if (!is_container_start(&p)) {
                parser_error(&p, "expected container (map or array) after section name");
                pasta_free(map);
                if (result) *result = p.result;
                return NULL;
            }
            PastaValue *container = parse_value(&p);
            if (!container || p.had_error) {
                pasta_free(map); pasta_free(container);
                if (result) *result = p.result;
                return NULL;
            }
            const char *sname; size_t slen;
            extract_section_name(&name_tok, &sname, &slen);
            pasta_map_put(map, sname, slen, container);
        }

        /* After all sections, must be EOF */
        if (!p.had_error && !check(&p, TOK_EOF)) {
            parser_error(&p, "expected '@' section or end of input");
            pasta_free(map);
            if (result) *result = p.result;
            return NULL;
        }

        if (result) *result = p.result;
        return map;
    }

    /*  Non-section mode: container-seq
        Parse the first value. If more containers follow (per the grammar),
        collect everything into an implicit top-level array.
        If there is only one container, return it directly. */
    PastaValue *first = parse_value(&p);

    if (p.had_error || check(&p, TOK_EOF)) {
        /* Single value or error — return as-is */
        if (result) *result = p.result;
        return first;
    }

    /* More tokens remain — try to parse additional containers */
    if (!is_container_start(&p)) {
        parser_error(&p, "expected container (map or array) at top level");
        pasta_free(first);
        if (result) *result = p.result;
        return NULL;
    }

    PastaValue *arr = pasta_value_array();
    if (!arr) { pasta_free(first); parser_error(&p, "allocation failed"); if (result) *result = p.result; return NULL; }
    pasta_array_push(arr, first);

    while (!p.had_error && !check(&p, TOK_EOF)) {
        if (!is_container_start(&p)) {
            parser_error(&p, "expected container (map or array) at top level");
            pasta_free(arr);
            if (result) *result = p.result;
            return NULL;
        }
        PastaValue *next = parse_value(&p);
        if (!next || p.had_error) { pasta_free(arr); pasta_free(next); if (result) *result = p.result; return NULL; }
        pasta_array_push(arr, next);
    }

    if (result) *result = p.result;
    return arr;
}

PASTA_API PastaValue *pasta_parse_cstr(const char *input, PastaResult *result) {
    return pasta_parse(input, strlen(input), result);
}
