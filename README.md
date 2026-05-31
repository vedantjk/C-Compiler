# cc89

A C compiler written from scratch in C++20, emitting x86-64 (System V / AT&T)
assembly. No parser generators, no codegen frameworks — the lexer, recursive
descent parser, semantic analyzer, intermediate representation, and x86-64
backend are all hand-written.

The compiler produces AT&T-syntax assembly targeting the System V ABI (Linux),
which is handed to `gcc` to assemble and link.

> The binary is named `cc89` for historical reasons. The project began aiming at
> C89; the scope has since grown to a much larger subset of C (see below). The
> name stuck.

## Language scope

cc89 compiles a growing subset of C. It is **not** a full ISO C compiler — there
is no preprocessor, and several language features are omitted.

**Supported**

- Types: `int`, `long`, `unsigned int`, `unsigned long`, `char` / `signed char` /
  `unsigned char`, `double`, `void`, pointers, arrays, and `struct` / `union`.
- All arithmetic, bitwise, logical, relational, and compound-assignment operators;
  pre/post increment and decrement; the ternary operator; `sizeof`; casts.
- Control flow: `if` / `else`, `while`, `do`-`while`, `for`, `break`, `continue`,
  `switch`, `goto` and labeled statements.
- Functions: definitions, calls, recursion, and separate translation units linked
  together.
- File-scope variables and the `static` / `extern` storage classes.
- String and character literals, and dynamic memory via `malloc` / `free`.

**Not supported (cut from scope)**

- The **preprocessor** — `#include`, `#define`, macros, and conditional compilation
  are not handled. Run the source through `cpp` (e.g. `gcc -E`) first if needed.
- `float`, `short`, `long long`, `_Bool`, and `enum`.
- `typedef`, bit-fields, and function pointers.
- Type qualifiers (`const`, `volatile`, `restrict`) — accepted in some positions
  but carry no semantics.
- Variable-length arrays, designated initializers, and compound literals.
- C11/C17 additions such as `_Atomic`, `_Generic`, and `<threads.h>`.

## Pipeline

```
source.c → [Lexer] → tokens → [Parser] → AST → [Semantic Analyzer] → typed AST
         → [TACKY IR] → three-address code → [Codegen] → x86-64 AT&T .s
```

### Lexer (`src/lexer`)

The lexer is a hand-written scanner that turns the source text into a flat stream
of tokens, each tagged with a kind, its lexeme, and a line/column position.
Positions are threaded through every later stage so diagnostics can point at the
exact spot in the source. Errors (an unterminated string, a stray character) are
reported through a shared `DiagnosticEngine` rather than thrown, so the lexer can
collect several problems in one pass before the compiler gives up.

### Parser (`src/parser`, `src/ast`)

The parser is recursive descent: one function per grammar production, producing an
abstract syntax tree of `shared_ptr` nodes (`src/ast`). Expressions use precedence
climbing — a single table maps each binary operator to a precedence level, and the
parser recurses with a rising minimum precedence to get associativity right
without a separate function per level. Postfix forms (calls, array subscripts,
`.`/`->` member access, and `++`/`--`) are parsed as a loop over a primary
expression, and a small declarator parser handles pointer and array types. The
parser only checks that the token stream is grammatically well-formed; it does not
resolve names or types.

### Semantic analyzer (`src/semanticanalyzer`)

Semantic analysis is a single centralized visitor that walks the AST and dispatches
on node type with `dynamic_cast`. It resolves identifiers against a scoped symbol
table, type-checks expressions, inserts implicit conversions, and marks which
expressions are lvalues — annotating each AST node in place with its resolved type
and symbol so later passes don't have to recompute any of it. Standard-library
functions (`printf`, `malloc`, `free`, `scanf`) are made visible through an
*auto-prelude*: a few C prototypes parsed through the real lexer and parser and
prepended to the program, so they flow through the same path as user code rather
than being special-cased.

### TACKY IR (`src/tacky`)

TACKY is the intermediate representation between the typed AST and assembly. It is
a flat, linear list of three-address instructions (`dst = src1 op src2`) operating
on temporaries and constants — closer to assembly than to C, but still
target-independent. The pass that builds it flattens nested expressions into
sequences of simple operations with fresh temporaries, and lowers all structured
control flow (`if`, loops, `&&`/`||`, the ternary) into conditional and
unconditional jumps between labels. It is deliberately **not** in SSA form, which
keeps the lowering and the backend simple.

### Codegen (`src/codegen`)

The backend lowers TACKY to x86-64 AT&T assembly in three passes over an assembly
IR. First, *instruction selection* translates each TACKY instruction into x86
instructions, still referring to operands by abstract "pseudoregisters" and
following the System V calling convention (first six integer arguments in
registers, the rest pushed on the stack, with 16-byte stack alignment). Second, a
*pseudo-replacement* pass assigns each pseudoregister a slot — a stack offset for
locals, or a RIP-relative address for static-storage variables — and computes the
frame size. Third, a *fixup* pass repairs instructions the ISA can't express
directly, such as memory-to-memory moves (routed through a scratch register) and
the operand constraints of `idiv`, shifts, and `imul`. The result is printed as an
assembly file.

## Status

- The **frontend** (lexing, parsing, semantic analysis) passes the full test
  suite.
- **End-to-end codegen** runs through the features above — local and file-scope
  variables, the full operator set, control flow, and functions with calls and
  recursion.
- Backend coverage of the later type features (pointers, arrays, structs,
  floating point) and optimization passes is in progress.

## Build

Requirements: a C++20 compiler and CMake ≥ 3.20.

```sh
cmake -S . -B build
cmake --build build
```

This produces the `cc89` executable in `build/`.

## Usage

```sh
cc89 [--lex | --parse | --validate | --tacky | --codegen] <source.c>
```

Each flag stops the pipeline at that stage; `--codegen` (the default) prints the
generated assembly to stdout. `--debugAST` and `--debugTacky` dump the AST and
the TACKY IR respectively.

To produce a runnable program, assemble and link the output with `gcc`:

```sh
cc89 --codegen program.c > program.s
gcc program.s -o program
```

## Testing

End-to-end tests live under `benchmarks/`, drawn from the *Writing a C Compiler*
test suite (Nora Sandler). A single Python runner, `tools/tt.py`, sweeps the tree
at a chosen pipeline stage and reports pass/fail. The `run` stage does
**differential testing against gcc**: it compiles each program with both cc89 and
gcc, runs both, and compares exit codes.

### Running the suite

Requirements: Python 3 and gcc (and a built `cc89`).

```sh
# build cc89, then run every implemented stage over the implemented chapters
python3 tools/tt.py build
python3 tools/tt.py run

# narrow the sweep
python3 tools/tt.py run 9            # one chapter, end-to-end
python3 tools/tt.py run 1-9          # a chapter range
python3 tools/tt.py parse            # stop at a single stage: lex|parse|validate|tacky|codegen|run
python3 tools/tt.py run path/to/foo.c   # a single file or directory

# inspect one program's output
python3 tools/tt.py asm path/to/foo.c   # print the assembly cc89 generated

# useful flags
python3 tools/tt.py run --extra-credit   # also sweep benchmarks/extra_credit/
python3 tools/tt.py run -v                # PASS/FAIL per file
python3 tools/tt.py run --no-build --cc89 build/cc89   # reuse a prebuilt binary
```

Exit codes: `0` all passed, `1` any failure, `2` bad arguments/environment.

The `run` stage assembles and links with `gcc`, so cc89 and gcc must share a
toolchain. On Windows, the `tt.ps1` wrapper runs the whole thing inside a Linux
Docker image to guarantee that — `.\tt.ps1 build-image` once, then
`.\tt.ps1 run 9` (every argument is forwarded to `tools/tt.py`). See
[`testing.md`](testing.md) for the full workflow.

### Lexer unit tests

```sh
cmake --build build --target test_lexer
./build/test_lexer
```

### CI

`.github/workflows/ci.yml` builds the project, runs the lexer tests under
**AddressSanitizer** and **Valgrind**, then runs the parse and execution suites
against the prebuilt binary.

## Layout

```
src/
├── lexer/             # tokenizer
├── parser/            # recursive descent parser
├── ast/               # AST node definitions
├── semanticanalyzer/  # type checking, symbol resolution
├── symboltable/       # symbol table
├── tacky/             # TACKY three-address IR
├── codegen/           # x86-64 backend
└── utils/             # diagnostics, shared helpers

benchmarks/            # test programs (valid / invalid, by chapter)
tools/tt.py            # test runner
```
