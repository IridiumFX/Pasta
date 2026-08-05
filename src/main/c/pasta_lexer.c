#include "pasta_internal.h"
#include <ctype.h>

void pasta_lexer_init(Lexer *lex, const char *src, size_t len) {
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

/* Forward declaration — definition is below alongside the other helpers. */
static int lex_remaining(const Lexer *lex);

static void skip_blank(Lexer *lex) {
    while (!lex_eof(lex)) {
        char c = lex_peek(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            lex_advance(lex);
        } else if (c == ';') {
            /* line comment: skip to end of line */
            while (!lex_eof(lex) && lex_peek(lex) != '\n')
                lex_advance(lex);
        } else if (c == '/' && lex_remaining(lex) >= 2 &&
                   lex->src[lex->pos + 1] == '/') {
            /* C99 line comment: skip to end of line */
            while (!lex_eof(lex) && lex_peek(lex) != '\n')
                lex_advance(lex);
        } else if (c == '/' && lex_remaining(lex) >= 2 &&
                   lex->src[lex->pos + 1] == '*') {
            /* C block comment: skip until close-delimiter */
            lex_advance(lex); /* consume / */
            lex_advance(lex); /* consume * */
            while (!lex_eof(lex)) {
                if (lex_peek(lex) == '*' && lex_remaining(lex) >= 2 &&
                    lex->src[lex->pos + 1] == '/') {
                    lex_advance(lex); /* consume * */
                    lex_advance(lex); /* consume / */
                    break;
                }
                lex_advance(lex);
            }
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

/* Strict number recogniser matching the grammar's `number` production:
       number  : float-constant | hex-number | bin-number
               | signed-integer , [ "." , digits ] ;
   No leading zeros in integers (007 is not a number); a fractional part needs
   at least one digit (1. is not a number); 0x / 0b need at least one digit.
   A lexeme that fails here is, in value position, a label-ref when it is a
   valid unquoted-label -- see the dispatcher. */
static int is_strict_number(const char *s, size_t n) {
    if (n == 0) return 0;
    size_t i = 0;
    if (s[0] == '-') { i = 1; if (n == 1) return 0; }
    const char *r = s + i;
    size_t rn = n - i;

    /* float-constants: Inf (and -Inf via the sign), NaN (unsigned only) */
    if (rn == 3 && memcmp(r, "Inf", 3) == 0) return 1;
    if (rn == 3 && memcmp(r, "NaN", 3) == 0) return i == 0;

    /* hex: 0 (x|X) hexdigit+ */
    if (rn >= 3 && r[0] == '0' && (r[1] == 'x' || r[1] == 'X')) {
        for (size_t k = 2; k < rn; k++)
            if (!isxdigit((unsigned char)r[k])) return 0;
        return 1;
    }
    /* bin: 0 (b|B) bindigit+ */
    if (rn >= 3 && r[0] == '0' && (r[1] == 'b' || r[1] == 'B')) {
        for (size_t k = 2; k < rn; k++)
            if (r[k] != '0' && r[k] != '1') return 0;
        return 1;
    }

    /* integer , [ "." , digits ]   with integer = "0" | natural { digit } */
    size_t k = 0;
    if (r[k] == '0') {
        k++;                              /* lone leading 0 permitted only alone */
    } else if (r[k] >= '1' && r[k] <= '9') {
        k++;
        while (k < rn && isdigit((unsigned char)r[k])) k++;
    } else {
        return 0;
    }
    if (k == rn) return 1;                /* pure integer                        */
    if (r[k] != '.') return 0;            /* trailing junk (e.g. 08, 0x, 0b)     */
    k++;                                  /* consume '.'                         */
    if (k == rn) return 0;               /* "1." needs a fractional digit       */
    while (k < rn && isdigit((unsigned char)r[k])) k++;
    return k == rn;                       /* reject e.g. 1.2.3                   */
}

/* Digit-led token: scan the maximal labelchar run, then classify.  A run that
   is a strict number is a number; otherwise it is a label (every character is
   a labelchar, so it is always a valid unquoted-label). */
static Token lex_number_or_label(Lexer *lex) {
    int line = lex->line, col = lex->col;
    const char *start = lex->src + lex->pos;
    while (!lex_eof(lex) && is_label_char(lex_peek(lex)))
        lex_advance(lex);
    size_t len = (size_t)(lex->src + lex->pos - start);
    return make_token(is_strict_number(start, len) ? TOK_NUMBER : TOK_LABEL,
                      start, len, line, col);
}

/* '-'-led token: '-' plus a labelchar run.  Because '-' is not a labelchar the
   result can only be a number; anything else (bare '-', -007, -abc) is an
   error, never a label. */
static Token lex_signed_number(Lexer *lex) {
    int line = lex->line, col = lex->col;
    const char *start = lex->src + lex->pos;
    lex_advance(lex);                     /* consume '-' */
    while (!lex_eof(lex) && is_label_char(lex_peek(lex)))
        lex_advance(lex);
    size_t len = (size_t)(lex->src + lex->pos - start);
    if (is_strict_number(start, len))
        return make_token(TOK_NUMBER, start, len, line, col);
    return error_token("invalid number", line, col);
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

Token pasta_lexer_next(Lexer *lex) {
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

    /* A digit-led run is a strict number or (by maximal munch) a label;
       a '-'-led run is a strict number or an error.  Keyword floats Inf/NaN
       are alpha-led and handled by lex_label_or_keyword; -Inf is recognised
       by is_strict_number inside lex_signed_number. */
    if (isdigit((unsigned char)c))
        return lex_number_or_label(lex);

    if (c == '-')
        return lex_signed_number(lex);

    if (isalpha((unsigned char)c) || is_label_symbol(c))
        return lex_label_or_keyword(lex);

    lex_advance(lex);
    return error_token("unexpected character", line, col);
}
