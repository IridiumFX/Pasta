#include "pasta_internal.h"
#include <ctype.h>

void lexer_init(Lexer *lex, const char *src, size_t len) {
    lex->src     = src;
    lex->src_len = len;
    lex->pos     = 0;
    lex->line    = 1;
    lex->col     = 1;
}

static int lex_eof(const Lexer *lex) {
    return lex->pos >= lex->src_len;
}

static char lex_peek(const Lexer *lex) {
    if (lex_eof(lex)) return '\0';
    return lex->src[lex->pos];
}

static char lex_advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else           { lex->col++; }
    return c;
}

static void skip_blank(Lexer *lex) {
    while (!lex_eof(lex)) {
        char c = lex_peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex_advance(lex);
        } else if (c == ';') {
            /* comment: skip to end of line */
            while (!lex_eof(lex) && lex_peek(lex) != '\n')
                lex_advance(lex);
        } else {
            break;
        }
    }
}

static int is_label_symbol(char c) {
    return c == '!' || c == '#' || c == '$' || c == '%'
        || c == '&' || c == '.' || c == '_';
}

static int is_label_char(char c) {
    return isalnum((unsigned char)c) || is_label_symbol(c);
}

static Token make_token(TokenType type, const char *start, size_t len, int line, int col) {
    Token t;
    t.type  = type;
    t.start = start;
    t.len   = len;
    t.line  = line;
    t.col   = col;
    return t;
}

static Token error_token(const char *msg, int line, int col) {
    Token t;
    t.type  = TOK_ERROR;
    t.start = msg;
    t.len   = strlen(msg);
    t.line  = line;
    t.col   = col;
    return t;
}

static int lex_remaining(const Lexer *lex) {
    return lex->src_len - lex->pos;
}

static Token lex_mstring(Lexer *lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    const char *start = lex->src + lex->pos;
    /* consume opening """ */
    lex_advance(lex); lex_advance(lex); lex_advance(lex);

    /* scan until closing """ or EOF */
    while (!lex_eof(lex)) {
        if (lex_peek(lex) == '"' && lex_remaining(lex) >= 3
            && lex->src[lex->pos + 1] == '"' && lex->src[lex->pos + 2] == '"') {
            lex_advance(lex); lex_advance(lex); lex_advance(lex);
            size_t len = (size_t)(lex->src + lex->pos - start);
            return make_token(TOK_MSTRING, start, len, start_line, start_col);
        }
        lex_advance(lex);
    }

    return error_token("unterminated multiline string", start_line, start_col);
}

static Token lex_string(Lexer *lex) {
    /* Check for triple-quote opening */
    if (lex_remaining(lex) >= 3
        && lex->src[lex->pos + 1] == '"' && lex->src[lex->pos + 2] == '"') {
        return lex_mstring(lex);
    }

    int start_line = lex->line;
    int start_col  = lex->col;
    const char *start = lex->src + lex->pos;
    lex_advance(lex); /* consume opening " */

    /* No escape sequences — scan until the next " or EOF */
    while (!lex_eof(lex) && lex_peek(lex) != '"')
        lex_advance(lex);

    if (lex_eof(lex))
        return error_token("unterminated string", start_line, start_col);

    lex_advance(lex); /* consume closing " */
    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(TOK_STRING, start, len, start_line, start_col);
}

static Token lex_number(Lexer *lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    const char *start = lex->src + lex->pos;

    if (lex_peek(lex) == '-') lex_advance(lex);

    while (!lex_eof(lex) && isdigit((unsigned char)lex_peek(lex)))
        lex_advance(lex);

    if (!lex_eof(lex) && lex_peek(lex) == '.') {
        lex_advance(lex);
        while (!lex_eof(lex) && isdigit((unsigned char)lex_peek(lex)))
            lex_advance(lex);
    }

    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(TOK_NUMBER, start, len, start_line, start_col);
}

static Token lex_label_or_keyword(Lexer *lex) {
    int start_line = lex->line;
    int start_col  = lex->col;
    const char *start = lex->src + lex->pos;

    while (!lex_eof(lex) && is_label_char(lex_peek(lex)))
        lex_advance(lex);

    size_t len = (size_t)(lex->src + lex->pos - start);

    if (len == 4 && memcmp(start, "true", 4) == 0)
        return make_token(TOK_TRUE, start, len, start_line, start_col);
    if (len == 5 && memcmp(start, "false", 5) == 0)
        return make_token(TOK_FALSE, start, len, start_line, start_col);
    if (len == 4 && memcmp(start, "null", 4) == 0)
        return make_token(TOK_NULL, start, len, start_line, start_col);
    if (len == 3 && memcmp(start, "Inf", 3) == 0)
        return make_token(TOK_NUMBER, start, len, start_line, start_col);
    if (len == 3 && memcmp(start, "NaN", 3) == 0)
        return make_token(TOK_NUMBER, start, len, start_line, start_col);

    return make_token(TOK_LABEL, start, len, start_line, start_col);
}

Token lexer_next(Lexer *lex) {
    skip_blank(lex);

    if (lex_eof(lex))
        return make_token(TOK_EOF, lex->src + lex->pos, 0, lex->line, lex->col);

    int line = lex->line;
    int col  = lex->col;
    char c   = lex_peek(lex);

    switch (c) {
        case '{': lex_advance(lex); return make_token(TOK_LBRACE,   lex->src + lex->pos - 1, 1, line, col);
        case '}': lex_advance(lex); return make_token(TOK_RBRACE,   lex->src + lex->pos - 1, 1, line, col);
        case '[': lex_advance(lex); return make_token(TOK_LBRACKET, lex->src + lex->pos - 1, 1, line, col);
        case ']': lex_advance(lex); return make_token(TOK_RBRACKET, lex->src + lex->pos - 1, 1, line, col);
        case ':': lex_advance(lex); return make_token(TOK_COLON,    lex->src + lex->pos - 1, 1, line, col);
        case ',': lex_advance(lex); return make_token(TOK_COMMA,    lex->src + lex->pos - 1, 1, line, col);
        case '"': return lex_string(lex);
        case '@': lex_advance(lex); return make_token(TOK_AT, lex->src + lex->pos - 1, 1, line, col);
        default:  break;
    }

    /* -Inf */
    if (c == '-' && lex->pos + 3 < lex->src_len && memcmp(lex->src + lex->pos, "-Inf", 4) == 0) {
        const char *start = lex->src + lex->pos;
        lex_advance(lex); lex_advance(lex); lex_advance(lex); lex_advance(lex);
        return make_token(TOK_NUMBER, start, 4, line, col);
    }

    if (c == '-' || isdigit((unsigned char)c))
        return lex_number(lex);

    if (isalpha((unsigned char)c) || is_label_symbol(c))
        return lex_label_or_keyword(lex);

    lex_advance(lex);
    return error_token("unexpected character", line, col);
}
