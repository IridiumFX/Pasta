#include "pasta_internal.h"

static PastaValue *alloc_value(PastaType type) {
    PastaValue *v = (PastaValue *)calloc(1, sizeof(PastaValue));
    if (v) v->type = type;
    return v;
}

PastaValue *pasta_value_null(void) {
    return alloc_value(PASTA_NULL);
}

PastaValue *pasta_value_bool(int b) {
    PastaValue *v = alloc_value(PASTA_BOOL);
    if (v) v->as.boolean = b;
    return v;
}

PastaValue *pasta_value_number(double n) {
    PastaValue *v = alloc_value(PASTA_NUMBER);
    if (v) v->as.number = n;
    return v;
}

PastaValue *pasta_value_string(const char *s, size_t len) {
    PastaValue *v = alloc_value(PASTA_STRING);
    if (!v) return NULL;
    v->as.string.data = (char *)malloc(len + 1);
    if (!v->as.string.data) { free(v); return NULL; }
    memcpy(v->as.string.data, s, len);
    v->as.string.data[len] = '\0';
    v->as.string.len = len;
    return v;
}

PastaValue *pasta_value_label(const char *s, size_t len) {
    PastaValue *v = alloc_value(PASTA_LABEL);
    if (!v) return NULL;
    v->as.string.data = (char *)malloc(len + 1);
    if (!v->as.string.data) { free(v); return NULL; }
    memcpy(v->as.string.data, s, len);
    v->as.string.data[len] = '\0';
    v->as.string.len = len;
    return v;
}

PastaValue *pasta_value_array(void) {
    PastaValue *v = alloc_value(PASTA_ARRAY);
    if (!v) return NULL;
    v->as.array.cap = 8;
    v->as.array.items = (PastaValue **)malloc(sizeof(PastaValue *) * v->as.array.cap);
    if (!v->as.array.items) { free(v); return NULL; }
    return v;
}

PastaValue *pasta_value_map(void) {
    PastaValue *v = alloc_value(PASTA_MAP);
    if (!v) return NULL;
    v->as.map.cap = 8;
    v->as.map.items = (PastaMember *)malloc(sizeof(PastaMember) * v->as.map.cap);
    if (!v->as.map.items) { free(v); return NULL; }
    return v;
}

int pasta_array_push(PastaValue *arr, PastaValue *item) {
    if (arr->as.array.count >= arr->as.array.cap) {
        size_t new_cap = arr->as.array.cap * 2;
        PastaValue **tmp = (PastaValue **)realloc(arr->as.array.items, sizeof(PastaValue *) * new_cap);
        if (!tmp) return -1;
        arr->as.array.items = tmp;
        arr->as.array.cap = new_cap;
    }
    arr->as.array.items[arr->as.array.count++] = item;
    return 0;
}

int pasta_map_put(PastaValue *map, const char *key, size_t key_len, PastaValue *value) {
    if (map->as.map.count >= map->as.map.cap) {
        size_t new_cap = map->as.map.cap * 2;
        PastaMember *tmp = (PastaMember *)realloc(map->as.map.items, sizeof(PastaMember) * new_cap);
        if (!tmp) return -1;
        map->as.map.items = tmp;
        map->as.map.cap = new_cap;
    }
    char *k = (char *)malloc(key_len + 1);
    if (!k) return -1;
    memcpy(k, key, key_len);
    k[key_len] = '\0';
    map->as.map.items[map->as.map.count].key   = k;
    map->as.map.items[map->as.map.count].value  = value;
    map->as.map.count++;
    return 0;
}

/* ---- Public Builder API ---- */

PASTA_API PastaValue *pasta_new_null(void) { return pasta_value_null(); }
PASTA_API PastaValue *pasta_new_bool(int b) { return pasta_value_bool(b); }
PASTA_API PastaValue *pasta_new_number(double n) { return pasta_value_number(n); }
PASTA_API PastaValue *pasta_new_string(const char *s) {
    return pasta_value_string(s, s ? strlen(s) : 0);
}
PASTA_API PastaValue *pasta_new_string_len(const char *s, size_t len) {
    return pasta_value_string(s, len);
}
PASTA_API PastaValue *pasta_new_label(const char *s) {
    return pasta_value_label(s, s ? strlen(s) : 0);
}
PASTA_API PastaValue *pasta_new_label_len(const char *s, size_t len) {
    return pasta_value_label(s, len);
}
PASTA_API PastaValue *pasta_new_array(void) { return pasta_value_array(); }
PASTA_API PastaValue *pasta_new_map(void) { return pasta_value_map(); }

PASTA_API int pasta_push(PastaValue *array, PastaValue *item) {
    if (!array || array->type != PASTA_ARRAY || !item) return -1;
    return pasta_array_push(array, item);
}

PASTA_API int pasta_set(PastaValue *map, const char *key, PastaValue *value) {
    if (!map || map->type != PASTA_MAP || !key || !value) return -1;
    return pasta_map_put(map, key, strlen(key), value);
}

PASTA_API int pasta_set_len(PastaValue *map, const char *key, size_t key_len, PastaValue *value) {
    if (!map || map->type != PASTA_MAP || !key || !value) return -1;
    return pasta_map_put(map, key, key_len, value);
}

/* ---- Public API ---- */

PASTA_API void pasta_free(PastaValue *v) {
    if (!v) return;
    switch (v->type) {
        case PASTA_STRING:
        case PASTA_LABEL:
            free(v->as.string.data);
            break;
        case PASTA_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++)
                pasta_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case PASTA_MAP:
            for (size_t i = 0; i < v->as.map.count; i++) {
                free(v->as.map.items[i].key);
                pasta_free(v->as.map.items[i].value);
            }
            free(v->as.map.items);
            break;
        default:
            break;
    }
    free(v);
}

PASTA_API PastaType pasta_type(const PastaValue *v) {
    return v ? v->type : PASTA_NULL;
}

PASTA_API int pasta_is_null(const PastaValue *v) {
    return !v || v->type == PASTA_NULL;
}

PASTA_API int pasta_get_bool(const PastaValue *v) {
    return (v && v->type == PASTA_BOOL) ? v->as.boolean : 0;
}

PASTA_API double pasta_get_number(const PastaValue *v) {
    return (v && v->type == PASTA_NUMBER) ? v->as.number : 0.0;
}

PASTA_API const char *pasta_get_string(const PastaValue *v) {
    return (v && v->type == PASTA_STRING) ? v->as.string.data : NULL;
}

PASTA_API size_t pasta_get_string_len(const PastaValue *v) {
    return (v && v->type == PASTA_STRING) ? v->as.string.len : 0;
}

PASTA_API const char *pasta_get_label(const PastaValue *v) {
    return (v && v->type == PASTA_LABEL) ? v->as.string.data : NULL;
}

PASTA_API size_t pasta_get_label_len(const PastaValue *v) {
    return (v && v->type == PASTA_LABEL) ? v->as.string.len : 0;
}

PASTA_API size_t pasta_count(const PastaValue *v) {
    if (!v) return 0;
    if (v->type == PASTA_ARRAY) return v->as.array.count;
    if (v->type == PASTA_MAP)   return v->as.map.count;
    return 0;
}

PASTA_API const PastaValue *pasta_array_get(const PastaValue *v, size_t index) {
    if (!v || v->type != PASTA_ARRAY || index >= v->as.array.count) return NULL;
    return v->as.array.items[index];
}

PASTA_API const PastaValue *pasta_map_get(const PastaValue *v, const char *key) {
    if (!v || v->type != PASTA_MAP || !key) return NULL;
    for (size_t i = 0; i < v->as.map.count; i++) {
        if (strcmp(v->as.map.items[i].key, key) == 0)
            return v->as.map.items[i].value;
    }
    return NULL;
}

PASTA_API const char *pasta_map_key(const PastaValue *v, size_t index) {
    if (!v || v->type != PASTA_MAP || index >= v->as.map.count) return NULL;
    return v->as.map.items[index].key;
}

PASTA_API const PastaValue *pasta_map_value(const PastaValue *v, size_t index) {
    if (!v || v->type != PASTA_MAP || index >= v->as.map.count) return NULL;
    return v->as.map.items[index].value;
}
