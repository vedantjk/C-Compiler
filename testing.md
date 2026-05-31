# Testing cc89

End-to-end tests live under `benchmarks/`. A single Python runner
(`tools/tt.py`) builds cc89, sweeps the tree at a chosen pipeline stage, and
reports pass/fail. It runs inside a Linux **Docker** image (so cc89 and `gcc`
share one toolchain — no Windows/WSL path or encoding pitfalls), and also runs
natively on Linux (CI) and on Windows for the non-`run` stages.

## Quick start (Windows)

```powershell
# once (or after editing the Dockerfile)
.\tt.ps1 build-image

.\tt.ps1 run 9                 # chapter 9, end-to-end (build + assemble + link + run)
.\tt.ps1 run path\to\foo.c     # a single file
.\tt.ps1 parse 1-9             # any stage over a chapter range
.\tt.ps1 asm path\to\foo.c     # print the assembly cc89 produced (+ exit code)
.\tt.ps1 run 9 --extra-credit  # include the extra-credit tree
.\tt.ps1 -v parse              # verbose; all implemented chapters
```

`.\tt.ps1 <args>` bind-mounts the repo into the `cc89-test` image and forwards
every argument to `tools/tt.py` — artifacts (`.s`, executables) land in
`build-docker/test-artifacts/` on the host, so you can open them directly.

## Quick start (Linux / CI / inside the container)

```bash
python3 tools/tt.py build
python3 tools/tt.py run 9
python3 tools/tt.py parse --no-build --cc89 build-valgrind/cc89   # prebuilt binary
```

## CLI

```
tt.py <command> [target] [options]

commands:
  build                              configure + build cc89 (+ test_lexer)
  lex|parse|validate|tacky|codegen|run   run that stage over the selected tests
  asm <file.c> [--save]              print cc89 --codegen for ONE file to stdout

target (optional for stage commands):
  (none)        all implemented mainline chapters
  9 | chapter_9 one chapter
  1-9           inclusive chapter range
  all           every chapter on disk (bypasses the implemented-chapters gate)
  <path>        a .c file or directory (bypasses the gate)

options:
  --extra-credit   also sweep benchmarks/extra_credit/ for the selected chapters
  -v, --verbose    PASS/FAIL per file
  --timeout N      per-process timeout in seconds (default 5)
  --no-build       skip the implicit build before a stage sweep
  --cc89 PATH      use this cc89 binary (skips building)
  --build-dir DIR  cmake build directory (default build-docker)
  --jobs N         build parallelism
```

Exit codes: `0` all passed, `1` any failure, `2` argument/environment error.

## Stages cc89 supports

cc89 takes one `--<stage>` flag and a source path; `--codegen` prints AT&T
x86-64 assembly to stdout. Stages: `lex`, `parse`, `validate`, `tacky`,
`codegen`, `compile` (default). The runner's `run` stage drives `--codegen`
then assembles+links+runs with `gcc` and compares the exit code to a gcc-built
reference.

## Layout

```
benchmarks/
├── *.c                                # ad-hoc sample programs (not swept by the runner)
├── nlsandler/                         # mainline suite (imported from nlsandler tests)
│   ├── parse_valid/chapter_N/...      # expect exit 0; eligible for the run suite
│   ├── parse_invalid/chapter_N/...    # expect non-zero (lex/parse rejects)
│   └── validate_invalid/chapter_N/... # exit 0 at lex/parse, non-zero at validate+
└── extra_credit/                      # goto/switch/labeled-statement & other extra tests
    ├── parse_valid/chapter_N/...      # only swept with --extra-credit
    ├── parse_invalid/chapter_N/...
    └── validate_invalid/chapter_N/...
```

The committed helper `nlsandler/parse_valid/chapter_9/stack_arguments/stack_alignment_check_linux.s`
defines `even_arguments`/`odd_arguments`; the runner links `stack_alignment.c`
against it. Library tests (`*/libraries/*`, `X.c` + `X_client.c` pairs) are
built with cc89 on one side and gcc on the other, then linked — an ABI check
against real gcc.

## Categories and expectations

| Path | lex/parse | validate/tacky/codegen | run |
| ---- | --------- | ---------------------- | --- |
| `parse_valid/**` | exit 0 | exit 0 | execute; exit code must match gcc |
| `parse_invalid/**` | non-zero | non-zero | skipped |
| `validate_invalid/**` | skipped | non-zero | skipped |

A test exceeding `--timeout` (default 5s) is a failure regardless of category.

## Implemented-chapters gate

`IMPLEMENTED_CHAPTERS` at the top of `tools/tt.py` lists the chapters whose
codegen is complete (currently 1–9). With no explicit target, the runner sweeps
only those chapters at every stage (later chapters use syntax the lexer/parser
can't handle yet). Explicit targets — a chapter, range, `all`, or a path —
bypass the gate. `--extra-credit` reuses the same chapter set; some extra-credit
tests exercise unimplemented features (goto/switch/...) and will surface as
failures at `run` — that's expected signal.

## CI and git hooks

- **CI** (`.github/workflows/ci.yml`, native Ubuntu): builds `cc89`/`test_lexer`,
  runs `test_lexer` under ASan and Valgrind, then `python3 tools/tt.py parse`
  and `python3 tools/tt.py run` against the prebuilt binary.
- **pre-commit** (Git Bash): clang-format staged sources, build, `test_lexer`,
  then `python tools/tt.py parse` with the Windows cc89 (no Docker — fast).
  Requires Python 3 on PATH; without it the parse sweep is skipped with a warning.
- **pre-push** (Docker): `tt.ps1 run` — the full codegen suite, identical to CI.

## Prerequisites

- **Docker** — for `tt.ps1` and the pre-push gate.
- **Python 3** on the Windows PATH — for the pre-commit parse sweep (optional;
  skipped if absent).
- **CLion / cmake** — your normal dev build (`cmake-build-debug/cc89.exe`).
