# cc89 — C89 Subset Compiler Design Document

## What This Is

A C89 subset compiler written in C++20, targeting x86-64 Linux (System V AMD64 ABI).
Hand-written lexer, hand-written recursive descent parser, custom IR, custom codegen.
No external dependencies beyond the C++ standard library.

---

## Scope

### v1 — In

- **Types**: `int`, `char`, pointers (`int *p`), arrays (`int a[10]`), structs
- **Declarations**: global variables, local variables, function declarations/definitions
- **Statements**: `if`/`else`, `while`, `for`, `return`, blocks `{}`
- **Expressions**: arithmetic (`+ - * / %`), comparisons (`== != < > <= >=`), logical (`&& || !`), bitwise (`& | ^ ~ << >>`), assignment (`=`), address-of (`&`), dereference (`*`), array subscript (`[]`), struct member (`.` and `->`), function calls, casts, `sizeof`
- **Functions**: parameters, return values, recursion
- **String literals**: as `char*` / `char[]`
- **Constants**: integer, character
- **Comments**: `/* */`

### v1 — Out

- `typedef`
- `union`
- bitfields
- function pointers
- `enum` (maybe v2)
- preprocessor (`#include`, `#define`, `#ifdef` — maybe minimal v2)
- `float`/`double`
- `switch`/`case` (maybe v2)
- `goto`
- `volatile`, `register`, `auto`, `extern`, `static` (storage class specifiers)
- variadic functions (`...`)
- multi-file compilation (v2)

### v2 — Later

- `enum`, `switch`/`case`
- minimal preprocessor (`#include`, `#define`)
- header + source file compilation
- `extern` / separate compilation + system linker
- basic optimizations (constant folding, dead code elimination)

---

## Architecture

```
source.c → Lexer → Tokens → Parser → AST → Semantic Analysis → IR → Register Allocation → x86-64 Assembly
```

### 1. Lexer (hand-written)

Reads source file character by character, produces token stream.
Tokens: keywords, identifiers, integer/char/string literals, operators, punctuation.
Reports errors with line:column.

### 2. Parser (hand-written recursive descent)

Consumes token stream, produces AST.
One function per grammar rule. Operator precedence handled by precedence climbing or Pratt parsing.
No `typedef` in v1 means no declaration/expression ambiguity — keeps it clean.
Syntax errors reported with context ("expected `;` after expression, got `}`").

### 3. AST

Tree of nodes: Program → top-level declarations → functions → statements → expressions.
Each node carries source location for error reporting.

### 4. Semantic Analysis

- Build symbol tables (scoped — global, function, block)
- Type checking: ensure operations are valid for their types
- Resolve struct member access
- Check function signatures match calls

### 5. IR (three-address code)

Similar to what was done in the Go compiler — a simple SSA-form IR with:
- Binary/unary operations
- Loads/stores
- Branches (conditional + unconditional)
- Function calls
- Phi nodes at join points
- Alloc (stack allocation)

This sits between the AST and codegen. Makes register allocation and (future) optimization easier.

### 6. Register Allocation

Two algorithms, selectable by flag:

**Linear scan** (default) — what you already implemented for the Go compiler. Fast, simple, good enough.

**Graph coloring** (flag: `--regalloc=graph`) — builds interference graph from live ranges, attempts to k-color it (k = number of allocatable registers). Produces better allocations, especially under register pressure. Spills when coloring fails.

### 7. x86-64 Codegen (GAS / AT&T syntax)

Emits `.s` assembly files. Assembled and linked via `gcc -o output output.s`.

---

## x86-64 Target Details

### System V AMD64 Calling Convention

| | Registers |
|---|---|
| **Integer args (in order)** | RDI, RSI, RDX, RCX, R8, R9 |
| **Return value** | RAX |
| **Caller-saved (volatile)** | RAX, RCX, RDX, RDI, RSI, R8, R9, R10, R11 |
| **Callee-saved (non-volatile)** | RBX, RBP, R12, R13, R14, R15 |
| **Stack pointer** | RSP (16-byte aligned before CALL) |
| **Frame pointer** | RBP (optional but useful for debugging) |

Additional args beyond 6 go on the stack. 128-byte red zone below RSP (usable without adjusting RSP).

### Registers

| 64-bit | 32-bit | 16-bit | 8-bit low | Notes |
|--------|--------|--------|-----------|-------|
| RAX | EAX | AX | AL | Return value, accumulator |
| RBX | EBX | BX | BL | Callee-saved |
| RCX | ECX | CX | CL | 4th arg, shift count |
| RDX | EDX | DX | DL | 3rd arg, multiply/divide |
| RSI | ESI | SI | SIL | 2nd arg |
| RDI | EDI | DI | DIL | 1st arg |
| RBP | EBP | BP | BPL | Frame pointer, callee-saved |
| RSP | ESP | SP | SPL | Stack pointer |
| R8 | R8D | R8W | R8B | 5th arg |
| R9 | R9D | R9W | R9B | 6th arg |
| R10 | R10D | R10W | R10B | Caller-saved, scratch |
| R11 | R11D | R11W | R11B | Caller-saved, scratch |
| R12 | R12D | R12W | R12B | Callee-saved |
| R13 | R13D | R13W | R13B | Callee-saved |
| R14 | R14D | R14W | R14B | Callee-saved |
| R15 | R15D | R15W | R15B | Callee-saved |

### Key Instructions (AT&T / GAS syntax)

```asm
# Data movement
movq  $42, %rax          # load immediate
movq  %rax, %rbx         # register to register
movq  (%rax), %rbx       # memory load
movq  %rbx, (%rax)       # memory store
leaq  8(%rsp), %rax      # load effective address

# Arithmetic
addq  %rsi, %rdi         # rdi += rsi
subq  %rsi, %rdi         # rdi -= rsi
imulq %rsi, %rdi         # rdi *= rsi (signed)
cqto                     # sign-extend RAX into RDX:RAX (for idivq)
idivq %rcx              # RDX:RAX / rcx → quotient RAX, remainder RDX
negq  %rax               # rax = -rax

# Bitwise
andq  %rsi, %rdi         # rdi &= rsi
orq   %rsi, %rdi         # rdi |= rsi
xorq  %rsi, %rdi         # rdi ^= rsi
notq  %rax               # rax = ~rax
salq  $3, %rax           # rax <<= 3 (arithmetic left shift)
sarq  $3, %rax           # rax >>= 3 (arithmetic right shift)
shrq  $3, %rax           # rax >>= 3 (logical right shift)

# Comparison + conditional
cmpq  %rsi, %rdi         # compare (sets flags for rdi - rsi)
sete  %al                # set byte to 1 if equal, 0 otherwise
setl  %al                # set byte to 1 if less (signed)
setg  %al                # set byte to 1 if greater (signed)
movzbq %al, %rax         # zero-extend byte to 64-bit

# Branches
jmp   .L1                # unconditional jump
je    .L1                # jump if equal (ZF=1)
jne   .L1                # jump if not equal
jl    .L1                # jump if less (signed)
jg    .L1                # jump if greater (signed)
jle   .L1                # jump if less or equal
jge   .L1                # jump if greater or equal

# Function call
call  func_name          # push return addr, jump
ret                      # pop return addr, jump back

# Stack
pushq %rbp               # rsp -= 8; [rsp] = rbp
popq  %rbp               # rbp = [rsp]; rsp += 8
subq  $32, %rsp          # allocate 32 bytes on stack
addq  $32, %rsp          # deallocate
```

---

## Build System

- **Build tool**: CMake 3.20+ with C++20
- **Compiler**: GCC 11.4 (on WSL Ubuntu 22.04)
- **Output assembly**: via `gcc -o output output.s` (GAS assembler + linker)
- **Binary name**: `cc89`
- **Build**: `cmake -B build && cmake --build build`
- **Usage**: `./build/cc89 source.c -o output.s && gcc -o output output.s`

---

## Testing Strategy

- **Diff against gcc**: compile same .c file with both cc89 and gcc, run both, compare output
- **c-testsuite**: https://github.com/c-testsuite/c-testsuite — ~220 small C programs for compiler testing
- **Own test programs**: incrementally write test .c files for each feature as you build it
- **chibicc's tests**: https://github.com/rui314/chibicc — has good test cases you can adapt

---

## Reading List

### Parser / Compiler Theory
- **Crafting Interpreters** by Robert Nystrom — free online, best intro to recursive descent
- **Engineering a Compiler** by Cooper & Torczon — modern compiler textbook, covers SSA and codegen
- **Dragon Book** (Aho, Lam, Sethi, Ullman) — classic reference

### C89 Grammar
- **ANSI C Yacc grammar**: https://www.lysator.liu.se/c/ANSI-C-grammar-y.html
- **ANSI C Lex grammar**: https://www.lysator.liu.se/c/ANSI-C-grammar-l.html

### Reference Implementations (READ THESE)
- **chibicc** — https://github.com/rui314/chibicc — THE reference. Clean, readable, incremental commits. Implements most of C11 in ~10k lines. Study the git history commit-by-commit.
- **8cc** — https://github.com/rui314/8cc — predecessor to chibicc, also by Rui Ueyama
- **lcc** — https://github.com/drh/lcc — retargetable C89 compiler, companion to the book "A Retargetable C Compiler" by Fraser & Hanson
- **tcc** — https://github.com/TinyCC/tinycc — Fabrice Bellard's Tiny C Compiler, one-pass design

### x86-64 Assembly
- **Felix Cloutier's x86 reference**: https://www.felixcloutier.com/x86/ — searchable instruction reference
- **Intel SDM**: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html — the bible
- **Stanford CS107 x86-64 guide**: https://web.stanford.edu/class/cs107/guide/x86-64.html
- **x86-64 Assembly Language Programming with Ubuntu** by Ed Jorgensen — free, covers GAS syntax on Linux
- **Eli Bendersky's blog on stack frames**: https://eli.thegreenplace.net/2011/09/06/stack-frame-layout-on-x86-64

### Graph Coloring Register Allocation
- Engineering a Compiler, Chapter 13
- Chaitin's original paper: "Register Allocation & Spilling via Graph Coloring" (1982)
- CMU 15-411 lecture notes on register allocation
