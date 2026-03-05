#ifndef PASTA_INTERNAL_H
#define PASTA_INTERNAL_H

#include "pasta.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Value representation ---- */

typedef struct PastaMember {
    char       *key;
    PastaValue *value;
} PastaMember;

struct PastaValue {
    PastaType type;
    union {
        int     boolean;
        double  number;
        struct { char *data; size_t len; } string;
        struct { PastaValue **items; size_t count; size_t cap; } array;
        struct { PastaMember *items;  size_t count; size_t cap; } map;
    } as;
};

/* ---- Value constructors (internal) ---- */

PastaValue *pasta_value_null(void);
PastaValue *pasta_value_bool(int b);
PastaValue *pasta_value_number(double n);
PastaValue *pasta_value_string(const char *s, size_t len);
PastaValue *pasta_value_array(void);
PastaValue *pasta_value_map(void);

int pasta_array_push(PastaValue *arr, PastaValue *item);
int pasta_map_put(PastaValue *map, const char *key, size_t key_len, PastaValue *value);

/* ---- Lexer ---- */

typedef enum {
    TOK_LBRACE,      /* { */
    TOK_RBRACE,      /* } */
    TOK_LBRACKET,    /* [ */
    TOK_RBRACKET,    /* ] */
    TOK_COLON,       /* : */
    TOK_COMMA,       /* , */
    TOK_STRING,
    TOK_MSTRING,     /* multiline """...""" */
    TOK_NUMBER,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NULL,
    TOK_LABEL,
    TOK_AT,          /* @ (section marker) */
    TOK_EOF,
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType   type;
    const char *start;
    size_t      len;
    int         line;
    int         col;
} Token;

typedef struct {
    const char *src;
    size_t      src_len;
    size_t      pos;
    int         line;
    int         col;
} Lexer;

void  lexer_init(Lexer *lex, const char *src, size_t len);
Token lexer_next(Lexer *lex);

#endif /* PASTA_INTERNAL_H */
