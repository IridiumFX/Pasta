#ifndef PASTA_H
#define PASTA_H

#include <stddef.h>

/* DLL export/import (PASTA_STATIC disables for static builds) */
#ifdef PASTA_STATIC
  #define PASTA_API
#elif defined(_WIN32)
  #ifdef PASTA_BUILDING
    #define PASTA_API __declspec(dllexport)
  #else
    #define PASTA_API __declspec(dllimport)
  #endif
#else
  #define PASTA_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Value types */
typedef enum {
    PASTA_NULL,
    PASTA_BOOL,
    PASTA_NUMBER,
    PASTA_STRING,
    PASTA_ARRAY,
    PASTA_MAP,
    PASTA_LABEL
} PastaType;

/* Opaque value handle */
typedef struct PastaValue PastaValue;

/* Error codes */
typedef enum {
    PASTA_OK = 0,
    PASTA_ERR_ALLOC,
    PASTA_ERR_SYNTAX,
    PASTA_ERR_UNEXPECTED_TOKEN,
    PASTA_ERR_UNEXPECTED_EOF
} PastaError;

/* Error info */
typedef struct {
    PastaError code;
    int        line;
    int        col;
    int        sections;      /* 1 if the document used @name sections */
    char       message[256];
} PastaResult;

/* ---- Parsing ---- */

PASTA_API PastaValue *pasta_parse(const char *input, size_t len, PastaResult *result);
PASTA_API PastaValue *pasta_parse_cstr(const char *input, PastaResult *result);
PASTA_API void        pasta_free(PastaValue *value);

/* ---- Querying ---- */

PASTA_API PastaType    pasta_type(const PastaValue *v);
PASTA_API int          pasta_is_null(const PastaValue *v);

/* Scalars */
PASTA_API int          pasta_get_bool(const PastaValue *v);
PASTA_API double       pasta_get_number(const PastaValue *v);
PASTA_API const char  *pasta_get_string(const PastaValue *v);
PASTA_API size_t       pasta_get_string_len(const PastaValue *v);
PASTA_API const char  *pasta_get_label(const PastaValue *v);
PASTA_API size_t       pasta_get_label_len(const PastaValue *v);

/* Containers */
PASTA_API size_t             pasta_count(const PastaValue *v);
PASTA_API const PastaValue  *pasta_array_get(const PastaValue *v, size_t index);
PASTA_API const PastaValue  *pasta_map_get(const PastaValue *v, const char *key);
PASTA_API const char        *pasta_map_key(const PastaValue *v, size_t index);
PASTA_API const PastaValue  *pasta_map_value(const PastaValue *v, size_t index);

/* ---- Building ---- */

/* Constructors — create new values. Caller owns the result (free with pasta_free). */
PASTA_API PastaValue *pasta_new_null(void);
PASTA_API PastaValue *pasta_new_bool(int b);
PASTA_API PastaValue *pasta_new_number(double n);
PASTA_API PastaValue *pasta_new_string(const char *s);
PASTA_API PastaValue *pasta_new_string_len(const char *s, size_t len);
PASTA_API PastaValue *pasta_new_label(const char *s);
PASTA_API PastaValue *pasta_new_label_len(const char *s, size_t len);
PASTA_API PastaValue *pasta_new_array(void);
PASTA_API PastaValue *pasta_new_map(void);

/* Mutators — returns 0 on success, -1 on error. Value takes ownership of item/value. */
PASTA_API int pasta_push(PastaValue *array, PastaValue *item);
PASTA_API int pasta_set(PastaValue *map, const char *key, PastaValue *value);
PASTA_API int pasta_set_len(PastaValue *map, const char *key, size_t key_len, PastaValue *value);

/* ---- Writing ---- */

/* Flags for pasta_write */
#define PASTA_PRETTY   0   /* indented, multiline (default) */
#define PASTA_COMPACT  1   /* single-line, minimal whitespace */
#define PASTA_SECTIONS 2   /* emit root map as @section containers (OR with PRETTY/COMPACT) */
#define PASTA_SORTED   4   /* sort map keys lexicographically (deterministic output) */

/* Serialize a value tree to a malloc'd Pasta string. Caller must free(). */
PASTA_API char *pasta_write(const PastaValue *v, int flags);

/* Write to a FILE*. Returns 0 on success, -1 on error. */
PASTA_API int   pasta_write_fp(const PastaValue *v, int flags, void *fp);

#ifdef __cplusplus
}
#endif

#endif /* PASTA_H */
