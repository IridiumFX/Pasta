# Pasta User Guide

**PASTA** stands for **Plain And Simple Text Archive**. It is a lightweight,
human-readable configuration format with a C library for reading and writing
Pasta documents.

---

## Table of Contents

1. [The Pasta Format](#1-the-pasta-format)
2. [The C Library](#2-the-c-library)
3. [Reading Pasta](#3-reading-pasta)
4. [Writing Pasta](#4-writing-pasta)
5. [Building Values](#5-building-values)
6. [Internals](#6-internals)

---

## 1. The Pasta Format

Pasta is a simplified alternative to JSON, designed for configuration files.
It keeps the parts of JSON that matter for config — maps, arrays, strings,
numbers, booleans, and null — while adding comments and relaxed label syntax.

### 1.1 Quick Example

```
; Database configuration
{
  host: "localhost",
  port: 5432,
  ssl: true,
  pool: {min: 2, max: 20},
  tags: ["primary", "production"],
  failover: null
}
```

### 1.2 Value Types

| Type      | Examples                          | Notes                                    |
|-----------|-----------------------------------|------------------------------------------|
| **null**  | `null`                            | The absence of a value.                  |
| **bool**  | `true`, `false`                   | Lowercase only.                          |
| **number**| `42`, `-3.14`, `0`, `1000000`     | Signed integers and decimals.            |
| **float constant** | `Inf`, `-Inf`, `NaN`    | IEEE 754 special values, case-sensitive. |
| **string**| `"hello"`, `"""multi\nline"""`    | Double-quoted; triple-quoted for multiline.|
| **array** | `[1, 2, 3]`                       | Ordered, comma-separated elements.       |
| **map**   | `{key: "value"}`                  | Unquoted or quoted keys, colon separator.|

### 1.3 Labels (Map Keys)

Map keys can be **unquoted labels** or **quoted strings**. An unquoted label is
one or more characters from the following set:

- Letters: `a`-`z`, `A`-`Z`
- Digits: `0`-`9`
- Symbols: `!` `#` `$` `%` `&` `_`

```
{
  simple: 1,
  with_underscore: 2,
  $price: 3,
  item#5: 4,
  bang!: 5
}
```

For keys containing characters outside the label set (dashes, dots, spaces,
etc.), use **quoted labels**:

```
{
  "Content-Type": "application/json",
  "server.host": "localhost",
  "with spaces": "value",
  regular: "bare labels still work"
}
```

This makes JSON-style maps valid Pasta — tools that emit `{"key": value}` now
produce parseable Pasta.

Labels are **case-sensitive**: `Name` and `name` are different keys.

**Note:** `@` is reserved for section markers and cannot appear in labels.
It can still be used inside strings.

### 1.4 Strings

Strings are enclosed in double quotes. There are **no escape sequences** —
every character between the quotes is taken literally, including backslashes.
The only character that cannot appear inside a string is the double quote `"`
itself, since it terminates the string.

```
{
  path: "C:\Users\Admin\Documents",
  greeting: "Hello, world!",
  symbols: "100% of @users & more"
}
```

Strings may contain any printable character, including spaces, punctuation,
and characters that are special in the grammar (`,`, `:`, `{`, `}`, etc.).

If you need a value that contains a double quote, use a multiline string.

### 1.4.1 Multiline Strings

For values that span multiple lines — CI scripts, Dockerfiles, SQL queries,
templates — use triple-quoted strings (`"""`):

```
{
  script: """
#!/bin/bash
set -euo pipefail
echo "Building..."
make all
make test
""",
  query: """SELECT u.name, u.email
FROM users u
WHERE u.active = true
ORDER BY u.name"""
}
```

Everything between the opening `"""` and closing `"""` is taken verbatim,
including newlines, indentation, and regular `"` characters. There are no
escape sequences. The only sequence that cannot appear inside a multiline
string is `"""` itself, since it terminates the string.

The writer automatically uses triple quotes when a string contains newlines,
and single quotes otherwise.

### 1.5 Comments

Comments start with a semicolon (`;`) and extend to the end of the line.
They can appear anywhere whitespace is allowed.

```
; This is a file-level comment
{
  ; Section comment
  key: 42,    ; Inline comment
  other: "hi"
}
```

### 1.6 Whitespace

Spaces, tabs (`0x09`), newlines (`0x0A`), and carriage returns (`0x0D`) are
all treated as blank space. Pasta is completely whitespace-insensitive between
tokens — you can write everything on one line or spread it across many:

```
; Compact
{a:1,b:2,c:[3,4]}

; Spread out
{
  a : 1 ,
  b : 2 ,
  c : [
    3 ,
    4
  ]
}
```

Both forms parse to the same value tree.

### 1.7 Sections

For documents with multiple root-level containers that need random access,
use **named sections** with `@`:

```
@global {
  project: "mega_platform",
  version: "4.2.0"
}

@services [
  {name: "api", replicas: 5},
  {name: "worker", replicas: 3}
]

@database {
  engine: "postgres",
  host: "db.internal",
  port: 5432
}
```

A section is `@name` followed by a container (`{...}` or `[...]`). Section
names follow the same rules as labels — use quoted names for special
characters: `@"my-section" {...}`.

Sections parse into a map, with section names as keys. In code:

```c
PastaValue *root = pasta_parse_cstr(input, NULL);
const PastaValue *db = pasta_map_get(root, "database");
double port = pasta_get_number(pasta_map_get(db, "port"));
```

**Rules:**
- If a document uses `@` sections, all root containers must be named.
- Section order is preserved (map key order = section order).
- Documents without `@` work exactly as before (single container or
  implicit array for multi-container).

To write sections back, use the `PASTA_SECTIONS` flag:

```c
char *out = pasta_write(root, PASTA_PRETTY | PASTA_SECTIONS);
```

Without `PASTA_SECTIONS`, the same data writes as a regular `{...}` map.

---

## 2. The C Library

### 2.1 Building

The library uses CMake (3.20+) and builds a shared library:
`libpasta.so` (Linux), `libpasta.dylib` (macOS), or `pasta.dll` (Windows).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Or using the bundled presets:

```bash
cmake --preset release
cmake --build --preset release
```

To disable the test executable:

```bash
cmake -B build -DPASTA_BUILD_TESTS=OFF
```

### 2.2 Linking

**CMake:**

```cmake
find_package(pasta REQUIRED)
target_link_libraries(your_target PRIVATE pasta)
```

**Manual (GCC/Clang):**

```bash
gcc -o myapp myapp.c -lpasta
```

**Windows:** The header automatically handles `__declspec(dllimport)` when you
include `pasta.h` without defining `PASTA_BUILDING`.

### 2.3 Header

Everything you need is in a single header:

```c
#include "pasta.h"
```

### 2.4 Project Layout

```
Pasta/
  CMakeLists.txt
  CMakePresets.json
  src/
    main/
      h/
        pasta.h              Public API
      c/
        pasta_internal.h     Internal types (not installed)
        pasta_lexer.c        Tokenizer
        pasta_parser.c       Recursive descent parser
        pasta_value.c        Value tree and query API
        pasta_writer.c       Serializer
    test/
      c/
        pasta_test.c         Test suite
  specs/
    Pasta.txt                BNF grammar
  docs/
    guide.md                 This file
```

---

## 3. Reading Pasta

### 3.1 Parsing

Two functions parse Pasta text into a value tree:

```c
// From a buffer with explicit length
PastaValue *pasta_parse(const char *input, size_t len, PastaResult *result);

// From a null-terminated string (convenience)
PastaValue *pasta_parse_cstr(const char *input, PastaResult *result);
```

Both return a `PastaValue*` on success or `NULL` on error. The optional
`PastaResult` struct receives error details.

**Basic usage:**

```c
PastaResult result;
PastaValue *root = pasta_parse_cstr(
    "{host: \"localhost\", port: 5432}", &result);

if (!root) {
    fprintf(stderr, "Parse error at %d:%d: %s\n",
            result.line, result.col, result.message);
    return 1;
}

// ... use root ...

pasta_free(root);
```

### 3.2 Error Handling

The `PastaResult` struct contains:

```c
typedef struct {
    PastaError code;       // Error code (PASTA_OK on success)
    int        line;       // 1-based line number
    int        col;        // 1-based column number
    char       message[256];
} PastaResult;
```

Error codes:

| Code                       | Meaning                                 |
|----------------------------|-----------------------------------------|
| `PASTA_OK`                 | Success.                                |
| `PASTA_ERR_ALLOC`          | Memory allocation failed.               |
| `PASTA_ERR_SYNTAX`         | General syntax error.                   |
| `PASTA_ERR_UNEXPECTED_TOKEN`| Token not valid in this position.      |
| `PASTA_ERR_UNEXPECTED_EOF` | Input ended before a complete value.    |

You may pass `NULL` instead of a `PastaResult*` if you don't need error details.

### 3.3 Querying Values

Every parsed value is a `PastaValue*`. First, check its type:

```c
PastaType pasta_type(const PastaValue *v);
int       pasta_is_null(const PastaValue *v);
```

`PastaType` is one of: `PASTA_NULL`, `PASTA_BOOL`, `PASTA_NUMBER`,
`PASTA_STRING`, `PASTA_ARRAY`, `PASTA_MAP`.

**Scalars:**

```c
int         pasta_get_bool(const PastaValue *v);       // 0 or 1
double      pasta_get_number(const PastaValue *v);     // IEEE 754 double
const char *pasta_get_string(const PastaValue *v);     // null-terminated
size_t      pasta_get_string_len(const PastaValue *v); // byte length
```

All scalar accessors are safe to call on any type — they return zero/NULL
for mismatched types.

**Containers:**

```c
size_t pasta_count(const PastaValue *v);  // element/member count
```

**Arrays:**

```c
const PastaValue *pasta_array_get(const PastaValue *v, size_t index);
```

Returns `NULL` for out-of-bounds access.

**Maps:**

```c
// Lookup by key name
const PastaValue *pasta_map_get(const PastaValue *v, const char *key);

// Iterate by index (preserves insertion order)
const char       *pasta_map_key(const PastaValue *v, size_t index);
const PastaValue *pasta_map_value(const PastaValue *v, size_t index);
```

`pasta_map_get` returns `NULL` if the key is not found. Index-based access
returns `NULL` for out-of-bounds.

### 3.4 Complete Read Example

```c
#include "pasta.h"
#include <stdio.h>

int main(void) {
    const char *input =
        "{\n"
        "  server: {host: \"0.0.0.0\", port: 8080},\n"
        "  database: {\n"
        "    host: \"db.internal\",\n"
        "    port: 5432,\n"
        "    pool: {min: 2, max: 20}\n"
        "  },\n"
        "  debug: false\n"
        "}";

    PastaResult r;
    PastaValue *cfg = pasta_parse_cstr(input, &r);
    if (!cfg) {
        fprintf(stderr, "Error: %s at %d:%d\n", r.message, r.line, r.col);
        return 1;
    }

    // Access nested values
    const PastaValue *db   = pasta_map_get(cfg, "database");
    const char       *host = pasta_get_string(pasta_map_get(db, "host"));
    double            port = pasta_get_number(pasta_map_get(db, "port"));
    double            pool_max = pasta_get_number(
                          pasta_map_get(pasta_map_get(db, "pool"), "max"));

    printf("DB: %s:%d (pool max: %d)\n", host, (int)port, (int)pool_max);

    // Iterate map keys
    const PastaValue *server = pasta_map_get(cfg, "server");
    for (size_t i = 0; i < pasta_count(server); i++) {
        printf("  server.%s\n", pasta_map_key(server, i));
    }

    pasta_free(cfg);
    return 0;
}
```

Output:
```
DB: db.internal:5432 (pool max: 20)
  server.host
  server.port
```

### 3.5 Memory

`pasta_parse` / `pasta_parse_cstr` allocate a tree of values on the heap.
Call `pasta_free` on the root to release the entire tree. You do not need
to free individual child values — freeing the root handles everything
recursively.

```c
PastaValue *root = pasta_parse_cstr("...", NULL);
// ... use root and its children ...
pasta_free(root);  // frees the entire tree
```

All `const PastaValue*` pointers returned by query functions point into the
tree owned by the root. They become invalid after `pasta_free(root)`.

Passing `NULL` to `pasta_free` is safe (it does nothing).

---

## 4. Writing Pasta

### 4.1 Serializing to a String

```c
char *pasta_write(const PastaValue *v, int flags);
```

Returns a `malloc`'d null-terminated string. The caller must `free()` it.
Returns `NULL` on allocation failure.

**Flags:**

| Flag             | Value | Output style                              |
|------------------|-------|-------------------------------------------|
| `PASTA_PRETTY`   | `0`   | Indented with newlines (default).         |
| `PASTA_COMPACT`  | `1`   | Single-line, minimal whitespace.          |
| `PASTA_SECTIONS` | `2`   | Emit root map as `@section` containers.   |
| `PASTA_SORTED`   | `4`   | Sort map keys lexicographically.          |

Flags can be combined with `|`: `PASTA_COMPACT | PASTA_SECTIONS | PASTA_SORTED`.

**Compact:**

```c
char *s = pasta_write(root, PASTA_COMPACT);
// -> {host: "localhost", port: 5432, ssl: true}
free(s);
```

**Pretty:**

```c
char *s = pasta_write(root, PASTA_PRETTY);
// -> {
//      host: "localhost",
//      port: 5432,
//      ssl: true
//    }
free(s);
```

Pretty output uses 2-space indentation and places each member/element on
its own line. Empty containers stay on one line (`[]`, `{}`).

**Sorted (canonical form):**

```c
char *s = pasta_write(root, PASTA_COMPACT | PASTA_SORTED);
// Keys are sorted lexicographically — same data always produces
// identical output, regardless of insertion order.
free(s);
```

`PASTA_SORTED` sorts map keys at every level of nesting, making the output
deterministic. This is useful for checksums, diffs, and version control.

### 4.2 Writing to a File

```c
int pasta_write_fp(const PastaValue *v, int flags, void *fp);
```

Writes directly to a `FILE*` (passed as `void*` to avoid requiring
`<stdio.h>` in the public header). Returns `0` on success, `-1` on error.

```c
#include <stdio.h>

FILE *fp = fopen("config.pasta", "w");
if (fp) {
    pasta_write_fp(root, PASTA_PRETTY, fp);
    fclose(fp);
}
```

### 4.3 Roundtrip Behaviour

The writer produces valid Pasta that can be parsed back to an identical
value tree:

```c
PastaValue *original = pasta_parse_cstr(input, NULL);
char       *text     = pasta_write(original, PASTA_COMPACT);
PastaValue *copy     = pasta_parse_cstr(text, NULL);
// 'copy' is structurally equal to 'original'
```

A few things to note about roundtrips:

- **Comments are stripped.** Comments are not part of the value tree, so they
  are lost when writing. This is by design.
- **Whitespace is normalized.** Regardless of the original formatting, the
  writer produces its own consistent style.
- **Key order is preserved.** Maps maintain insertion order, so keys come
  out in the same order they were parsed.
- **Strings are verbatim.** There are no escape sequences, so strings
  roundtrip byte-for-byte.
- **Number precision.** Integers roundtrip exactly. Decimals use full
  `double` precision (`%.17g`) to avoid silent loss.

### 4.4 NULL Safety

`pasta_write(NULL, flags)` returns `"null"` (a heap-allocated copy). It
does not crash.

---

## 5. Building Values

The builder API lets you construct `PastaValue` trees programmatically —
no parsing required. Build a tree, then write it out with `pasta_write`.

### 5.1 Constructors

```c
PastaValue *pasta_new_null(void);
PastaValue *pasta_new_bool(int b);
PastaValue *pasta_new_number(double n);
PastaValue *pasta_new_string(const char *s);         // from C string
PastaValue *pasta_new_string_len(const char *s, size_t len); // from buffer
PastaValue *pasta_new_array(void);
PastaValue *pasta_new_map(void);
```

All constructors return a heap-allocated `PastaValue*`. Free with
`pasta_free` when done (freeing a root frees the entire tree).

### 5.2 Mutators

```c
int pasta_push(PastaValue *array, PastaValue *item);
int pasta_set(PastaValue *map, const char *key, PastaValue *value);
int pasta_set_len(PastaValue *map, const char *key, size_t key_len, PastaValue *value);
```

`pasta_push` appends an item to an array. `pasta_set` adds a key-value
pair to a map. Both return `0` on success, `-1` on error (wrong type or
allocation failure). The container takes ownership of the child value.

### 5.3 Example

```c
#include "pasta.h"
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    PastaValue *root = pasta_new_map();

    pasta_set(root, "name", pasta_new_string("my_service"));
    pasta_set(root, "port", pasta_new_number(8080));
    pasta_set(root, "debug", pasta_new_bool(0));

    PastaValue *tags = pasta_new_array();
    pasta_push(tags, pasta_new_string("go"));
    pasta_push(tags, pasta_new_string("api"));
    pasta_set(root, "tags", tags);

    PastaValue *db = pasta_new_map();
    pasta_set(db, "host", pasta_new_string("localhost"));
    pasta_set(db, "port", pasta_new_number(5432));
    pasta_set(db, "ssl", pasta_new_bool(1));
    pasta_set(root, "database", db);

    char *output = pasta_write(root, PASTA_PRETTY);
    printf("%s\n", output);
    free(output);

    pasta_free(root);
    return 0;
}
```

Output:
```
{
  name: "my_service",
  port: 8080,
  debug: false,
  tags: [
    "go",
    "api"
  ],
  database: {
    host: "localhost",
    port: 5432,
    ssl: true
  }
}
```

### 5.4 Ownership

When you pass a value to `pasta_push` or `pasta_set`, the container takes
ownership. Do **not** free child values individually — freeing the root
handles everything. Do **not** insert the same value into multiple
containers (each value must have exactly one owner).

---

## 6. Internals

This section describes the library's internal architecture. You don't need
this to use the library, but it's helpful if you want to understand or
modify the code.

### 6.1 Architecture Overview

```
  Input text
      |
      v
  +--------+     +--------+     +-----------+
  | Lexer  | --> | Parser | --> | Value Tree|
  +--------+     +--------+     +-----------+
                                      |
                                      v
                                 +--------+
                                 | Writer |
                                 +--------+
                                      |
                                      v
                                 Output text
```

The pipeline has four stages, each in its own source file:

| File                | Role                                      |
|---------------------|-------------------------------------------|
| `pasta_lexer.c`     | Tokenizer: text to token stream.          |
| `pasta_parser.c`    | Parser: token stream to value tree.       |
| `pasta_value.c`     | Value tree: constructors, accessors, free.|
| `pasta_writer.c`    | Serializer: value tree to text.           |

### 6.2 Lexer

The lexer (`Lexer` struct) scans input one character at a time and produces
`Token` values. It handles:

- **Blank skipping:** spaces, tabs, newlines, and `;` comments are consumed
  between tokens.
- **Single-character tokens:** `{`, `}`, `[`, `]`, `:`, `,`.
- **Strings:** scans from opening `"` to closing `"`. Detects `"""` for multiline.
- **Numbers:** digits with optional leading `-` and optional `.` decimal part.
- **Keywords/Labels:** alphabetic or symbol-starting sequences are classified
  as `true`/`false`/`null`/`Inf`/`NaN` (keywords) or labels (map keys).

The lexer tracks line and column numbers for error reporting. It does not
allocate memory — tokens are slices (`start` pointer + `len`) into the
original input buffer.

Token types:

```
TOK_LBRACE  TOK_RBRACE  TOK_LBRACKET  TOK_RBRACKET
TOK_COLON   TOK_COMMA   TOK_STRING    TOK_NUMBER
TOK_TRUE    TOK_FALSE   TOK_NULL      TOK_LABEL
TOK_EOF     TOK_ERROR
```

### 6.3 Parser

The parser is a **recursive descent** parser with one token of lookahead.
It consumes the token stream from the lexer and builds a `PastaValue` tree.

Key functions map directly to the grammar:

| Function        | Grammar rule                          |
|-----------------|---------------------------------------|
| `parse_value`   | `value = container | type | void`     |
| `parse_map`     | `map = "{" members "}"`               |
| `parse_array`   | `array = "[" elements "]"`            |

The parser owns a `Parser` struct that wraps the lexer and tracks the
current token plus error state. On the first error, it sets `had_error`
and all subsequent calls short-circuit and return `NULL`.

String values are copied verbatim from between the quotes — there are no
escape sequences to process. Numbers are converted to `double` via
`strtod`, with special handling for `Inf`, `-Inf`, and `NaN`.

### 6.4 Value Tree

The central data structure is `PastaValue`, defined in `pasta_internal.h`:

```c
struct PastaValue {
    PastaType type;
    union {
        int     boolean;
        double  number;
        struct { char *data; size_t len; }        string;
        struct { PastaValue **items; size_t count; size_t cap; } array;
        struct { PastaMember *items;  size_t count; size_t cap; } map;
    } as;
};
```

Each value is a tagged union. Containers (arrays and maps) use dynamic
arrays that grow by doubling capacity.

Map members are stored as `PastaMember` structs (key string + value pointer)
in insertion order. Key lookup is a linear scan — suitable for configuration
files, which typically have small maps.

The public API exposes `PastaValue` as an opaque pointer. All access goes
through the accessor functions in `pasta.h`, which perform type and bounds
checks and return safe defaults (0, NULL) for mismatched types.

`pasta_free` recursively walks the tree and frees all allocations: strings,
array item arrays, map member arrays, member key strings, and the value
nodes themselves.

### 6.5 Writer

The writer serializes a `PastaValue` tree back to Pasta text. It uses a
simple dynamic buffer (`Buf` struct) that grows as needed.

The writer walks the tree recursively:

- **Scalars** are written directly (`null`, `true`, `false`, numbers, quoted
  strings). Strings containing newlines use triple-quoted `"""` syntax.
- **Arrays** and **maps** are written with either compact or pretty formatting
  depending on the flags.
- **Pretty mode** tracks indentation depth and emits 2-space indents,
  newlines between elements, and trailing commas between (not after) items.
- **Compact mode** uses `, ` as the separator with no newlines.

The writer does not produce comments — since comments are not part of the
value tree, they cannot survive a parse-write roundtrip.

### 6.6 Platform Considerations

**Shared library exports:** The `PASTA_API` macro handles visibility:

- Windows: `__declspec(dllexport)` when building, `__declspec(dllimport)`
  when consuming.
- Linux/macOS: `__attribute__((visibility("default")))` with hidden default
  visibility (`-fvisibility=hidden`).

**Cross-platform builds:** The CMake project produces:

| Platform | Shared library        | Link library         |
|----------|-----------------------|----------------------|
| Linux    | `libpasta.so.0.1.0`  | (same, via soname)   |
| macOS    | `libpasta.0.1.0.dylib` | (same, via install name) |
| Windows  | `pasta.dll`           | `pasta.lib` / `libpasta.dll.a` |

**C standard:** The library requires C11 (for `<math.h>` classification
macros like `isnan`, `isinf`). It has no external dependencies beyond the
C standard library.
