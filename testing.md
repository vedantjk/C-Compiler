# Testing cc89

End-to-end tests live under `benchmarks/`. A small Bash runner
(`benchmarks/run_tests.sh`) sweeps the tree, invokes `cc89` on each `.c`
file at a chosen pipeline stage, and reports pass/fail.

## Quick start

```bash
# parse-stage sweep over everything in benchmarks/ (default — but needs
# cc89 to recognize --parse, see "Stage flags" below)
./benchmarks/run_tests.sh

# Today's cc89 has no flag handling yet — pass --stage compile to skip
# the flag and just invoke cc89 with the source path:
./benchmarks/run_tests.sh --stage compile

# only run nlsandler parse_valid
./benchmarks/run_tests.sh --stage compile --filter parse_valid/

# specific paths
./benchmarks/run_tests.sh --stage compile benchmarks/sa benchmarks/simple1.c

# verbose — print PASS/FAIL per file
./benchmarks/run_tests.sh -v --stage compile --filter chapter_1/

# tighter or looser per-test timeout (default 5s)
./benchmarks/run_tests.sh --stage compile --timeout 10s
```

The runner finds the binary by checking, in order:
`$CC89`, `cmake-build-debug/cc89[.exe]`, `cmake-build-release/cc89[.exe]`,
`build/cc89[.exe]`, `out/build/x64-Debug/cc89.exe`,
`out/build/x64-Release/cc89.exe`. Override with `CC89=/path/to/binary`.

## Stage flags cc89 must support

The runner passes a `--<stage>` flag as `argv[1]` and the source path as
`argv[2]`. The current `main.cpp` accepts only the source path, so until
the flags are wired up, run with `--stage compile` (which passes no flag).

| Stage | Flag | What cc89 should do | Exit code |
| ----- | ---- | ------------------- | --------- |
| `lex` | `--lex` | Tokenize only. No parsing. | 0 on lex success, non-0 on lex error. |
| `parse` | `--parse` | Lex + parse. Build AST. No semantic checks. | 0 on parse success, non-0 on lex/parse error. |
| `validate` | `--validate` | Lex + parse + semantic analysis. Print diagnostics to stderr. | 0 if SA reports no errors, non-0 otherwise. |
| `tacky` | `--tacky` | Through IR generation. | 0 on success. |
| `codegen` | `--codegen` | Through codegen, no assembly written. | 0 on success. |
| `compile` | *(no flag)* | Full pipeline. (default `main.cpp` behavior) | 0 on success. |

Suggested wiring in `main.cpp`:

```cpp
int main(int argc, char **argv) {
    std::string stage = "compile";
    std::string path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a.rfind("--", 0) == 0) stage = a.substr(2);
        else path = a;
    }
    // ...lex...
    if (stage == "lex") return 0;
    // ...parse...
    if (stage == "parse") return 0;
    // ...semantic analysis...
    if (stage == "validate") return 0;
    // ...etc.
}
```

For semantic-error tests under `benchmarks/sa/`, cc89 should print the
diagnostic messages to **stderr** (one error per line) and exit non-zero.
The runner diffs cc89's stderr against the sibling `expected` file.

## Layout

```
benchmarks/
├── run_tests.sh                    # the runner
├── *.c                             # ad-hoc parser/lexer tests (test.c, simple1.c, ...)
├── sa/<name>/                      # semantic-analysis golden tests
│   ├── <name>.c
│   └── expected                    # exact stderr cc89 should emit at --validate
└── nlsandler/                      # imported from nlsandler/writing-a-c-compiler-tests
    ├── .import.py                  # re-runnable import script
    ├── parse_valid/chapter_N/...   # cc89 should exit 0
    └── parse_invalid/chapter_N/... # cc89 should exit non-0 (parse error expected)
```

## Test categories and expectations

The runner classifies each file by path and applies these rules:

| Path pattern | Expected behavior |
| ------------ | ----------------- |
| `nlsandler/parse_valid/**/*.c` | exit 0 at any stage. |
| `nlsandler/parse_invalid/**/*.c` | exit non-0 at any stage (lex or parse error). |
| `sa/<name>/<name>.c` | At `lex`/`parse`/`compile`: exit 0 (these parse cleanly). At `validate`/`tacky`/`codegen`: exit non-0 *and* stderr matches `<name>/expected`. `compile` is not checked because the runner can't know whether your `compile` pipeline runs SA yet. |
| anything else under `benchmarks/` | exit 0 at any stage. |

A test that hangs more than `--timeout` (default 5s) is recorded as a
TIMEOUT failure regardless of category — these surface infinite loops in
cc89, like the file-scope-label case in `parse_invalid/chapter_10/`.

## Re-importing the nlsandler suite

```bash
# clone once
git clone --depth 1 \
    https://github.com/nlsandler/writing-a-c-compiler-tests.git \
    "$TEMP/wacc-tests"   # or /tmp/wacc-tests

# regenerate benchmarks/nlsandler/parse_{valid,invalid}/
python benchmarks/nlsandler/.import.py
```

The script is idempotent — it deletes `parse_valid/` and `parse_invalid/`
before writing.

### What gets filtered

The nlsandler suite targets a much larger C dialect than cc89. The
importer drops files containing any of these tokens:

- Type keywords cc89 doesn't model: `long`, `short`, `unsigned`, `signed`,
  `float`, `double`, `_Bool`/`bool`.
- Storage classes / qualifiers cc89 doesn't parse: `static`, `extern`,
  `register`, `auto`, `const`, `volatile`, `inline`, `restrict`, `typedef`.
- Constructs cc89 doesn't lex: `goto`/labels, `switch`/`case`/`default`,
  `enum`, `union`.
- C99-only syntax: `//` comments, `for (int i = 0; ...)` declarations
  in for-init.
- Preprocessor directives — cc89 has no preprocessor, so any `#ifdef`,
  `#pragma`, `#include`, `#define` line drops the file.

Whole chapters are skipped: 11–13 (long/unsigned/double), 17 (heap), 19
(optimization), 20 (register allocation). Chapters 1–10 and 14–16, 18
are imported.

`invalid_semantics`, `invalid_types`, `invalid_declarations`,
`invalid_labels`, `invalid_struct_tags` directories are also skipped — they
need semantic analysis to reject, and cc89's parser lets them through.
Add them once SA is online.

### Brace-adding transform

cc89's parser deliberately rejects brace-less control-flow bodies: the
body of `if`, `while`, `for`, `do-while`, and `else` must be `{...}`
(see memory: "Control-flow bodies must be blocks"). nlsandler tests
freely use brace-less form, so the importer rewrites:

```c
if (cond)            =>     if (cond)
    stmt;                       { stmt; }

if (a) X else if (b) Y   =>   if (a) X else { if (b) Y }
```

Only valid tests get this rewrite (invalid_parse tests need to keep
their original form so the parse error still triggers). Tests whose
*entire purpose* is the brace-less form (e.g. `if_null_body.c`) are
dropped via filename match.

## Adding new tests

- Put parser / lexer ad-hoc tests as `benchmarks/<name>.c`. They're
  expected to parse cleanly.
- Put semantic-analysis tests as `benchmarks/sa/<name>/<name>.c` with a
  sibling `expected` file containing the exact stderr cc89 should emit at
  `--validate`.
- Don't hand-edit anything under `benchmarks/nlsandler/` — re-run the
  importer instead.

## Current baseline (cc89 at parser stage, `--stage compile`)

```
total:  310
passed: 286
failed:  24
```

The 24 failures split into two groups, both signal:

**12 `parse_valid` failures — cc89 parser gaps:**

- **Mixed declarations** — cc89 enforces strict C89 "decls before
  statements" inside a block (`parser.h` `parseBlockStmt`). Tests like
  `chapter_5/exp_then_declaration.c`, `chapter_7/hidden_then_visible.c`,
  `chapter_8/multi_break.c` violate that.
- **Empty `for` slots** — `for (;;)` and `for (init;cond;)` aren't
  accepted; `parseForStmt` calls `parseExpression()` unconditionally on
  each slot. See `chapter_8/null_for_header.c`,
  `chapter_8/for_absent_post.c`.
- **Dangling-else with brace-less inner ifs** — the brace-add transform
  is conservative around nested if/else and gets the binding wrong on
  `chapter_6/if_nested_5.c`.
- **Parenthesized declarators** — `int (*foo(...))[3]` returning a
  pointer to an array isn't supported (`parser.h:140` throws explicitly).
  See `chapter_15/declarators/return_nested_array.c`. Per memory, this
  is intentionally dropped from v1.
- **Function declarations inside blocks** — `int foo(void);` as a
  block-level declaration. See `chapter_9/no_arguments/*.c`.

**12 `parse_invalid` failures — cc89 too lenient or hangs:**

- 8 cases where cc89 accepts (exit 0) input that should be rejected:
  illegal characters in chapter_1 lex tests, missing semicolons,
  bad escape sequences in char/string literals (chapter 16). The lexer
  is currently permissive about these.
- 4 TIMEOUT cases — `ParseProgram` infinite-loops when the top-level
  cursor is on a non-type token (e.g. file-scope label `x:`, extra
  closing brace). The `while (peek() != EOF_TOKEN)` loop has no
  `else throw`/`else consume()` for the `!isTypeStart(peek())` branch.
  See `parser.h:611-643`.

These are useful failure modes — fix the parser and watch the green
count climb.

## Failure exit codes

The runner exits `0` if every test passed, `1` if any test failed,
`2` on argument or environment errors (unknown stage, missing binary).
