#include "pasta_internal.h"
#include <stdio.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/*  Dynamic string buffer                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} Buf;

static int buf_init(Buf *b) {
    b->cap  = 256;
    b->len  = 0;
    b->data = (char *)malloc(b->cap);
    return b->data ? 0 : -1;
}

static int buf_grow(Buf *b, size_t need) {
    if (b->len + need < b->cap) return 0;
    size_t new_cap = b->cap * 2;
    while (new_cap < b->len + need) new_cap *= 2;
    char *tmp = (char *)realloc(b->data, new_cap);
    if (!tmp) return -1;
    b->data = tmp;
    b->cap  = new_cap;
    return 0;
}

static int buf_append(Buf *b, const char *s, size_t n) {
    if (buf_grow(b, n + 1)) return -1;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

static int buf_puts(Buf *b, const char *s) {
    return buf_append(b, s, strlen(s));
}

static int buf_putc(Buf *b, char c) {
    return buf_append(b, &c, 1);
}

/* ------------------------------------------------------------------ */
/*  Indent helper                                                      */
/* ------------------------------------------------------------------ */

static int buf_indent(Buf *b, int depth) {
    for (int i = 0; i < depth; i++) {
        if (buf_puts(b, "  ")) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Label writing                                                      */
/* ------------------------------------------------------------------ */

static int is_label_symbol(char c) {
    return c == '!' || c == '#' || c == '$' || c == '%'
        || c == '&' || c == '_';
}

static int is_label_char(char c) {
    return isalnum((unsigned char)c) || is_label_symbol(c);
}

static int is_keyword(const char *s, size_t len) {
    return (len == 4 && memcmp(s, "true", 4) == 0)
        || (len == 5 && memcmp(s, "false", 5) == 0)
        || (len == 4 && memcmp(s, "null", 4) == 0)
        || (len == 3 && memcmp(s, "Inf", 3) == 0)
        || (len == 3 && memcmp(s, "NaN", 3) == 0);
}

/* Check whether a key can be written as a bare label */
static int is_bare_label(const char *s) {
    size_t len = strlen(s);
    if (len == 0) return 0;
    for (size_t i = 0; i < len; i++) {
        if (!is_label_char(s[i])) return 0;
    }
    if (is_keyword(s, len)) return 0;
    return 1;
}

static int write_label(Buf *b, const char *key) {
    if (is_bare_label(key)) {
        return buf_puts(b, key);
    }
    /* Quoted label */
    if (buf_putc(b, '"')) return -1;
    if (buf_puts(b, key)) return -1;
    if (buf_putc(b, '"')) return -1;
    return 0;
}

/* ------------------------------------------------------------------ */
/*  String writing                                                     */
/* ------------------------------------------------------------------ */

static int has_newline(const char *s, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n' || s[i] == '\r') return 1;
    return 0;
}

static int write_string(Buf *b, const char *s, size_t len) {
    if (has_newline(s, len)) {
        if (buf_puts(b, "\"\"\"")) return -1;
        if (buf_append(b, s, len)) return -1;
        if (buf_puts(b, "\"\"\"")) return -1;
    } else {
        if (buf_putc(b, '"')) return -1;
        if (buf_append(b, s, len)) return -1;
        if (buf_putc(b, '"')) return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Number formatting                                                  */
/* ------------------------------------------------------------------ */

static int write_number(Buf *b, double n) {
    char tmp[64];
    if (isnan(n)) {
        return buf_puts(b, "NaN");
    } else if (isinf(n)) {
        return buf_puts(b, n < 0 ? "-Inf" : "Inf");
    }
    /* Use %g but strip trailing zeros; if the number is integral, omit dot */
    if (n == (long long)n && n >= -1e15 && n <= 1e15) {
        snprintf(tmp, sizeof(tmp), "%lld", (long long)n);
    } else {
        snprintf(tmp, sizeof(tmp), "%.17g", n);
    }
    return buf_puts(b, tmp);
}

/* ------------------------------------------------------------------ */
/*  Sorted index helper                                                */
/* ------------------------------------------------------------------ */

/* Build a sorted index array for map members. Caller must free(). */
static size_t *sorted_indices(const PastaMember *items, size_t count) {
    size_t *idx = (size_t *)malloc(count * sizeof(size_t));
    if (!idx) return NULL;
    for (size_t i = 0; i < count; i++) idx[i] = i;
    /* Simple insertion sort — config maps are small */
    for (size_t i = 1; i < count; i++) {
        size_t tmp = idx[i];
        size_t j = i;
        while (j > 0 && strcmp(items[idx[j - 1]].key, items[tmp].key) > 0) {
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = tmp;
    }
    return idx;
}

/* ------------------------------------------------------------------ */
/*  Recursive writer                                                   */
/* ------------------------------------------------------------------ */

static int write_value(Buf *b, const PastaValue *v, int compact, int sorted, int depth);

static int write_array(Buf *b, const PastaValue *v, int compact, int sorted, int depth) {
    size_t count = v->as.array.count;
    if (count == 0) return buf_puts(b, "[]");

    if (buf_putc(b, '[')) return -1;

    for (size_t i = 0; i < count; i++) {
        if (compact) {
            if (i > 0 && buf_puts(b, ", ")) return -1;
        } else {
            if (buf_putc(b, '\n')) return -1;
            if (buf_indent(b, depth + 1)) return -1;
        }
        if (write_value(b, v->as.array.items[i], compact, sorted, depth + 1)) return -1;
        if (!compact && i + 1 < count) {
            if (buf_putc(b, ',')) return -1;
        }
    }

    if (!compact) {
        if (buf_putc(b, '\n')) return -1;
        if (buf_indent(b, depth)) return -1;
    }
    if (buf_putc(b, ']')) return -1;
    return 0;
}

static int write_map(Buf *b, const PastaValue *v, int compact, int sorted, int depth) {
    size_t count = v->as.map.count;
    if (count == 0) return buf_puts(b, "{}");

    size_t *order = NULL;
    if (sorted && count > 1) {
        order = sorted_indices(v->as.map.items, count);
        if (!order) return -1;
    }

    if (buf_putc(b, '{')) return -1;

    for (size_t n = 0; n < count; n++) {
        size_t i = order ? order[n] : n;
        if (compact) {
            if (n > 0 && buf_puts(b, ", ")) { free(order); return -1; }
        } else {
            if (buf_putc(b, '\n')) { free(order); return -1; }
            if (buf_indent(b, depth + 1)) { free(order); return -1; }
        }
        if (write_label(b, v->as.map.items[i].key)) { free(order); return -1; }
        if (buf_puts(b, ": ")) { free(order); return -1; }
        if (write_value(b, v->as.map.items[i].value, compact, sorted, depth + 1)) { free(order); return -1; }
        if (!compact && n + 1 < count) {
            if (buf_putc(b, ',')) { free(order); return -1; }
        }
    }

    free(order);

    if (!compact) {
        if (buf_putc(b, '\n')) return -1;
        if (buf_indent(b, depth)) return -1;
    }
    if (buf_putc(b, '}')) return -1;
    return 0;
}

static int write_value(Buf *b, const PastaValue *v, int compact, int sorted, int depth) {
    if (!v) return buf_puts(b, "null");

    switch (v->type) {
    case PASTA_NULL:   return buf_puts(b, "null");
    case PASTA_BOOL:   return buf_puts(b, v->as.boolean ? "true" : "false");
    case PASTA_NUMBER: return write_number(b, v->as.number);
    case PASTA_STRING: return write_string(b, v->as.string.data, v->as.string.len);
    case PASTA_LABEL:  return buf_append(b, v->as.string.data, v->as.string.len);
    case PASTA_ARRAY:  return write_array(b, v, compact, sorted, depth);
    case PASTA_MAP:    return write_map(b, v, compact, sorted, depth);
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/*  Section writer                                                     */
/* ------------------------------------------------------------------ */

static int write_sections(Buf *b, const PastaValue *v, int compact, int sorted) {
    if (!v || v->type != PASTA_MAP) return write_value(b, v, compact, sorted, 0);

    size_t count = v->as.map.count;
    size_t *order = NULL;
    if (sorted && count > 1) {
        order = sorted_indices(v->as.map.items, count);
        if (!order) return -1;
    }

    for (size_t n = 0; n < count; n++) {
        size_t i = order ? order[n] : n;
        if (n > 0) {
            if (buf_putc(b, '\n')) { free(order); return -1; }
        }
        if (buf_putc(b, '@')) { free(order); return -1; }
        if (write_label(b, v->as.map.items[i].key)) { free(order); return -1; }
        if (compact) {
            if (buf_putc(b, ' ')) { free(order); return -1; }
        } else {
            if (buf_putc(b, '\n')) { free(order); return -1; }
        }
        if (write_value(b, v->as.map.items[i].value, compact, sorted, 0)) { free(order); return -1; }
        if (!compact) {
            if (buf_putc(b, '\n')) { free(order); return -1; }
        }
    }
    free(order);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

PASTA_API char *pasta_write(const PastaValue *v, int flags) {
    Buf b;
    if (buf_init(&b)) return NULL;

    int compact  = (flags & PASTA_COMPACT) != 0;
    int sections = (flags & PASTA_SECTIONS) != 0;
    int sorted   = (flags & PASTA_SORTED) != 0;

    int err;
    if (sections) {
        err = write_sections(&b, v, compact, sorted);
    } else {
        err = write_value(&b, v, compact, sorted, 0);
    }

    if (err) {
        free(b.data);
        return NULL;
    }

    /* Ensure trailing newline for pretty output */
    if (!compact && b.len > 0 && b.data[b.len - 1] != '\n') {
        buf_putc(&b, '\n');
    }

    return b.data;
}

PASTA_API int pasta_write_fp(const PastaValue *v, int flags, void *fp) {
    char *s = pasta_write(v, flags);
    if (!s) return -1;
    int ret = fputs(s, (FILE *)fp) == EOF ? -1 : 0;
    free(s);
    return ret;
}
