# Pasta conformance corpus — labels & atoms

A portable, implementation-independent test corpus that pins the behaviour of
the **label / atom grammar**: how a bare run of label characters is classified
in value position, and how map keys and section names are read.

It exists because the grammar was published before any parser, and independent
implementors — following the spec faithfully — hit the same anomaly (a label
spelled like a number, e.g. `123abc` or a `0:`/`true:` key). Now that a
reference implementation exists, this corpus freezes the agreed behaviour so
any implementation can self-check against the same cases.

## Files

| File | What it is |
|---|---|
| `atom-labels.cases` | Label / atom grammar: `input → expected structural fingerprint`. 91 cases. |
| `comments.cases` | Comment grammar (`;`, `//`, `/* */`). 16 cases. |
| `strings.cases` | String grammar: raw strings and the multiline quote-run rule. 14 cases. |
| `run_conformance.c` | Reference runner (public Pasta API only). Proves the reference implementation matches the corpora, and serves as a worked example for other runners. |

The `.cases` files are the source of truth; the runner takes one as its argument.

## Corpus format

Plain text. Lines starting with `;` are comments; blank lines are ignored.
Each case is two consecutive content lines:

```
> <input>          the input document (after the "> " prefix)
= <fingerprint>    the expected structural fingerprint, or ERR
```

Within `<input>` **and** `<fingerprint>` alike, `\n` means a newline and `\\` a
literal backslash, so a case can span lines — which line comments and multiline
strings both need. No other escape is recognised.

The **fingerprint** is a serializer-independent encoding of the parse tree, so
it does not depend on any implementation's whitespace or number formatting:

```
null | true | false | num | str:TEXT | lbl:TEXT | blob   scalars (by kind)
[f,f,...]                                                array, in order
{k=f,k=f,...}                                            map, document order; k = key text verbatim
ERR                                                      input must be rejected
```

Scalars are encoded by *kind*: every number is `num` (the corpus asserts that a
token is a number, not which number), booleans and null by name, strings and
labels with their text. This is exactly the distinction the label grammar turns
on — is a token a `num`, a `lbl:…`, a keyword, or the key text of a map.

## What it covers

- Maximal munch: digit-led runs that are not valid numbers are single labels
  (`123abc`, `0x1fg`, `1_000`, `12.3.4`).
- Value-position precedence: `keyword > number > label-ref`
  (`true`→bool, `0`→num, `foo`→label).
- Keys and section names are `label`: `{0: 1}`, `{true: false}`, `@0 { … }`.
- `-` is not a labelchar: `{-5: 1}` and `@-5 { … }` are errors (quote to use).
- Regressions that must stay invalid (`{key value}`, `{a: }`, `[1,,2]`).
- All three comment forms (`;`, `//`, `/* */`) as `blank`; that delimiters inside
  strings are data, not comments (`"http://x/y"`); and that an unterminated block
  comment or a lone `/` is an error.
- Strings are raw — no escape sequences, so a backslash is an ordinary character
  — and the multiline quote-run rule: the body ends at the first run of three or
  more quotes, and extras in that run are content, which is how content ending
  in a quote is written (`"""ends q""""` → `ends q"`).

Blob values are Basta-only and binary, so they are not in this text corpus; the
Basta library suite covers them. Apart from blobs, Pasta and Basta accept exactly
the same language — the case lines here are identical to those in Basta's copy of
these corpora (only the headers differ, each citing its own spec), and keeping
them so is the check that the superset claim still holds.

## Running the reference runner

From this directory, built straight from the library sources:

```bash
gcc -std=c11 -DPASTA_STATIC -I../../src/main/h -I../../src/main/c run_conformance.c ../../src/main/c/pasta_*.c -o run_conformance
```

Then run each corpus:

```bash
./run_conformance atom-labels.cases && ./run_conformance comments.cases && ./run_conformance strings.cases
```

Expected tails:

```
conformance: 91/91 passed
conformance: 16/16 passed
conformance: 14/14 passed
```

Exit status is `0` iff every case matches.

## Checking another implementation

Write a runner in your language that, for each case, parses the input and emits
the fingerprint above (or reports a parse error for `ERR` cases), then diff its
output against the `=` lines. Any deviation is a conformance gap. `run_conformance.c`
is a ~120-line template to copy.
