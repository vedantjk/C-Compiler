#!/usr/bin/env bash
# run_tests.sh — drive cc89 over benchmarks/ at a chosen pipeline stage.
#
# Usage:
#   benchmarks/run_tests.sh [--stage STAGE] [--filter PATTERN] [-v] [path ...]
#
# Stages (passed to cc89 as `--<stage>` flag, except `compile` which omits it):
#   lex       lex only
#   parse     lex + parse                      (default — current cc89 behavior)
#   validate  lex + parse + semantic analysis
#   tacky     + IR
#   codegen   + codegen
#   compile   full pipeline (no flag)
#   run       full pipeline + assemble+link+run; exit code must match system gcc.
#             Only runs files in chapters listed in CHAPTERS_IMPLEMENTED below;
#             other files are reported as skipped.
#
# Test categories (inferred from path):
#   benchmarks/nlsandler/parse_valid/**/*.c     expect exit 0 (any stage)
#   benchmarks/nlsandler/parse_invalid/**/*.c   expect exit != 0 (any stage)
#   benchmarks/sa/<name>/<name>.c               at stage >= validate: stderr must
#                                               match sibling `expected` file;
#                                               at earlier stages: expect exit 0
#   benchmarks/*.c (and anywhere else)          expect exit 0
#
# Examples:
#   benchmarks/run_tests.sh
#   benchmarks/run_tests.sh --stage lex
#   benchmarks/run_tests.sh --stage validate --filter sa/
#   benchmarks/run_tests.sh benchmarks/nlsandler/parse_valid/chapter_1

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

STAGE="parse"
FILTER=""
VERBOSE=0
TIMEOUT="5s"
PATHS=()

# Chapters whose codegen is complete enough to run end-to-end. Bump per chapter.
CHAPTERS_IMPLEMENTED=(chapter_1 chapter_2 chapter_3 chapter_4 chapter_5 chapter_6 chapter_7 chapter_8 chapter_9)
ARTIFACT_DIR="$REPO_ROOT/cmake-build-debug/test-artifacts"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --stage)   STAGE="$2"; shift 2 ;;
        --stage=*) STAGE="${1#--stage=}"; shift ;;
        --filter)  FILTER="$2"; shift 2 ;;
        --filter=*) FILTER="${1#--filter=}"; shift ;;
        --timeout) TIMEOUT="$2"; shift 2 ;;
        --timeout=*) TIMEOUT="${1#--timeout=}"; shift ;;
        -v|--verbose) VERBOSE=1; shift ;;
        -h|--help)
            sed -n '2,30p' "$0"; exit 0 ;;
        --) shift; PATHS+=("$@"); break ;;
        *)  PATHS+=("$1"); shift ;;
    esac
done

case "$STAGE" in
    lex|parse|validate|tacky|codegen|compile|run) ;;
    *) echo "unknown stage: $STAGE" >&2; exit 2 ;;
esac

if [[ "$STAGE" == "run" ]]; then
    if ! command -v gcc >/dev/null 2>&1; then
        echo "stage=run needs gcc on PATH (MinGW or system)" >&2
        exit 2
    fi
    mkdir -p "$ARTIFACT_DIR"
fi

# ----- locate cc89 binary -----------------------------------------------------
find_cc89() {
    if [[ -n "${CC89:-}" && -x "$CC89" ]]; then
        echo "$CC89"; return
    fi
    local candidates=(
        "$REPO_ROOT/cmake-build-debug/cc89.exe"
        "$REPO_ROOT/cmake-build-debug/cc89"
        "$REPO_ROOT/cmake-build-release/cc89.exe"
        "$REPO_ROOT/cmake-build-release/cc89"
        "$REPO_ROOT/build/cc89.exe"
        "$REPO_ROOT/build/cc89"
        "$REPO_ROOT/out/build/x64-Debug/cc89.exe"
        "$REPO_ROOT/out/build/x64-Release/cc89.exe"
    )
    for c in "${candidates[@]}"; do
        [[ -x "$c" ]] && { echo "$c"; return; }
    done
    return 1
}

CC89_BIN="$(find_cc89)" || {
    echo "could not locate cc89 binary. Set \$CC89 or build first." >&2
    exit 2
}
[[ $VERBOSE -eq 1 ]] && echo "using cc89: $CC89_BIN"

# ----- build flag -------------------------------------------------------------
STAGE_FLAG=""
if [[ "$STAGE" != "compile" ]]; then
    STAGE_FLAG="--$STAGE"
fi

# ----- categorize a path ------------------------------------------------------
# echoes one of: parse_valid | parse_invalid | sa | other
classify() {
    local p="$1"
    case "$p" in
        *benchmarks/nlsandler/parse_valid/*) echo parse_valid ;;
        *benchmarks/nlsandler/parse_invalid/*) echo parse_invalid ;;
        *benchmarks/nlsandler/validate_invalid/*) echo validate_invalid ;;
        *benchmarks/sa/*) echo sa ;;
        *) echo other ;;
    esac
}

# ----- gather test files ------------------------------------------------------
if [[ ${#PATHS[@]} -eq 0 ]]; then
    PATHS=("$SCRIPT_DIR")
fi

TEST_FILES=()
for p in "${PATHS[@]}"; do
    if [[ -f "$p" && "$p" == *.c ]]; then
        TEST_FILES+=("$p")
    elif [[ -d "$p" ]]; then
        while IFS= read -r f; do
            TEST_FILES+=("$f")
        done < <(find "$p" -type f -name '*.c' | sort)
    else
        echo "skipping non-existent path: $p" >&2
    fi
done

if [[ -n "$FILTER" ]]; then
    FILTERED=()
    for f in "${TEST_FILES[@]}"; do
        [[ "$f" == *"$FILTER"* ]] && FILTERED+=("$f")
    done
    TEST_FILES=("${FILTERED[@]+"${FILTERED[@]}"}")
fi

# Extra-credit tests (goto, switch/case, labeled statements) cover features cc89
# doesn't implement yet. They stay in the repo as part of the target suite, but
# are never executed at any stage. Drop this block to re-enable them once those
# features land.
if [[ ${#TEST_FILES[@]} -gt 0 ]]; then
    KEPT=()
    for f in "${TEST_FILES[@]}"; do
        [[ "$f" == *"/extra_credit/"* ]] && continue
        KEPT+=("$f")
    done
    TEST_FILES=("${KEPT[@]+"${KEPT[@]}"}")
fi

if [[ ${#TEST_FILES[@]} -eq 0 ]]; then
    echo "no test files matched"; exit 1
fi

# ----- run --------------------------------------------------------------------
PASS=0
FAIL=0
SKIP=0
FAILED_FILES=()

# Returns 0 if the file's path contains a chapter dir in CHAPTERS_IMPLEMENTED.
# Matching the whole path (not just the immediate parent) lets an implemented
# chapter also cover its extra_credit/ subdir, while keeping later chapters'
# extra_credit gated by their own (unimplemented) chapter.
is_chapter_implemented() {
    local f="$1"
    for c in "${CHAPTERS_IMPLEMENTED[@]}"; do
        [[ "$f" == *"/$c/"* ]] && return 0
    done
    return 1
}

# run_one_execute: cc89 --codegen -> .s, gcc assemble+link -> .exe,
# gcc reference build -> .ref.exe, run both, compare exit codes.
# Only called when STAGE=run, on parse_valid files in implemented chapters.
run_one_execute() {
    local f="$1"
    local base chapter artifact_base asm_file our_exe ref_exe our_exit ref_exit
    local reason="" ok=1

    base="${f##*/}"; base="${base%.c}"
    chapter="$(basename "$(dirname "$f")")"
    artifact_base="${chapter}_${base}"
    asm_file="$ARTIFACT_DIR/${artifact_base}.s"
    our_exe="$ARTIFACT_DIR/${artifact_base}.exe"
    ref_exe="$ARTIFACT_DIR/${artifact_base}.ref.exe"

    if ! timeout "$TIMEOUT" "$CC89_BIN" --codegen "$f" >"$asm_file" 2>/dev/null; then
        ok=0; reason="cc89 --codegen failed"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$asm_file" -o "$our_exe" 2>/dev/null; then
        ok=0; reason="gcc could not assemble/link cc89 output"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$f" -o "$ref_exe" 2>/dev/null; then
        ok=0; reason="reference gcc build failed"
    fi
    if [[ $ok -eq 1 ]]; then
        timeout "$TIMEOUT" "$our_exe" >/dev/null 2>&1; our_exit=$?
        timeout "$TIMEOUT" "$ref_exe" >/dev/null 2>&1; ref_exit=$?
        if [[ $our_exit -ne $ref_exit ]]; then
            ok=0; reason="exit mismatch: cc89=$our_exit gcc=$ref_exit"
        fi
    fi

    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS+1))
        [[ $VERBOSE -eq 1 ]] && echo "PASS $f"
    else
        FAIL=$((FAIL+1))
        FAILED_FILES+=("$f ($reason)")
        [[ $VERBOSE -eq 1 ]] && echo "FAIL $f ($reason)"
    fi
}

# library tests come in pairs: foo.c (defines functions, no main) and
# foo_client.c (has main, calls them). Map either file to its sibling.
library_sibling() {
    local f="$1" dir base stem
    dir="$(dirname "$f")"; base="${f##*/}"; stem="${base%.c}"
    if [[ "$stem" == *_client ]]; then
        echo "$dir/${stem%_client}.c"
    else
        echo "$dir/${stem}_client.c"
    fi
}

# run_one_library: build the file-under-test with cc89 and its sibling with gcc,
# link them, and compare exit code to an all-gcc build of the pair. Mixing
# compilers is deliberate — it checks cc89's calling convention against gcc's.
# Each file in a pair gets its own turn under cc89 as the loop visits it.
run_one_library() {
    local f="$1"
    local sib base chapter artifact_base asm_file our_obj sib_obj our_exe ref_exe our_exit ref_exit
    local reason="" ok=1

    sib="$(library_sibling "$f")"
    if [[ ! -f "$sib" ]]; then
        run_one_execute "$f"; return   # no pair found; fall back to single-file
    fi

    base="${f##*/}"; base="${base%.c}"
    chapter="$(basename "$(dirname "$f")")"
    artifact_base="${chapter}_${base}__pair"
    asm_file="$ARTIFACT_DIR/${artifact_base}.s"
    our_obj="$ARTIFACT_DIR/${artifact_base}.o"
    sib_obj="$ARTIFACT_DIR/${artifact_base}.sib.o"
    our_exe="$ARTIFACT_DIR/${artifact_base}.exe"
    ref_exe="$ARTIFACT_DIR/${artifact_base}.ref.exe"

    if ! timeout "$TIMEOUT" "$CC89_BIN" --codegen "$f" >"$asm_file" 2>/dev/null; then
        ok=0; reason="cc89 --codegen failed"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w -c "$asm_file" -o "$our_obj" 2>/dev/null; then
        ok=0; reason="gcc could not assemble cc89 output"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w -c "$sib" -o "$sib_obj" 2>/dev/null; then
        ok=0; reason="gcc could not compile sibling"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$our_obj" "$sib_obj" -o "$our_exe" 2>/dev/null; then
        ok=0; reason="link of cc89+gcc objects failed"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$f" "$sib" -o "$ref_exe" 2>/dev/null; then
        ok=0; reason="reference gcc build failed"
    fi
    if [[ $ok -eq 1 ]]; then
        timeout "$TIMEOUT" "$our_exe" >/dev/null 2>&1; our_exit=$?
        timeout "$TIMEOUT" "$ref_exe" >/dev/null 2>&1; ref_exit=$?
        if [[ $our_exit -ne $ref_exit ]]; then
            ok=0; reason="exit mismatch: cc89-pair=$our_exit gcc=$ref_exit"
        fi
    fi

    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS+1))
        [[ $VERBOSE -eq 1 ]] && echo "PASS $f (library pair with $(basename "$sib"))"
    else
        FAIL=$((FAIL+1))
        FAILED_FILES+=("$f ($reason)")
        [[ $VERBOSE -eq 1 ]] && echo "FAIL $f ($reason)"
    fi
}

# run_one_with_helper: build the test file with cc89, link it against an extra
# helper object (e.g. the platform stack_alignment_check_<platform>.s that
# defines even_arguments/odd_arguments), and compare exit code to a gcc build of
# the same pair.
run_one_with_helper() {
    local f="$1" helper="$2"
    local base chapter artifact_base asm_file our_exe ref_exe our_exit ref_exit
    local reason="" ok=1

    base="${f##*/}"; base="${base%.c}"
    chapter="$(basename "$(dirname "$f")")"
    artifact_base="${chapter}_${base}"
    asm_file="$ARTIFACT_DIR/${artifact_base}.s"
    our_exe="$ARTIFACT_DIR/${artifact_base}.exe"
    ref_exe="$ARTIFACT_DIR/${artifact_base}.ref.exe"

    if ! timeout "$TIMEOUT" "$CC89_BIN" --codegen "$f" >"$asm_file" 2>/dev/null; then
        ok=0; reason="cc89 --codegen failed"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$asm_file" "$helper" -o "$our_exe" 2>/dev/null; then
        ok=0; reason="gcc could not assemble/link cc89 output"
    fi
    if [[ $ok -eq 1 ]] && ! gcc -w "$f" "$helper" -o "$ref_exe" 2>/dev/null; then
        ok=0; reason="reference gcc build failed"
    fi
    if [[ $ok -eq 1 ]]; then
        timeout "$TIMEOUT" "$our_exe" >/dev/null 2>&1; our_exit=$?
        timeout "$TIMEOUT" "$ref_exe" >/dev/null 2>&1; ref_exit=$?
        if [[ $our_exit -ne $ref_exit ]]; then
            ok=0; reason="exit mismatch: cc89=$our_exit gcc=$ref_exit"
        fi
    fi

    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS+1))
        [[ $VERBOSE -eq 1 ]] && echo "PASS $f (linked with $(basename "$helper"))"
    else
        FAIL=$((FAIL+1))
        FAILED_FILES+=("$f ($reason)")
        [[ $VERBOSE -eq 1 ]] && echo "FAIL $f ($reason)"
    fi
}

run_one() {
    local f="$1"
    local kind exp_exit got_exit got_err
    kind="$(classify "$f")"

    # default expectations: exit 0
    exp_exit=0

    if [[ "$kind" == "parse_invalid" ]]; then
        exp_exit=nonzero
    fi
    # validate_invalid/ tests parse cleanly but must be rejected by semantic
    # analysis: expect exit 0 at the parse stage, non-zero once SA runs (validate+).
    if [[ "$kind" == "validate_invalid" && ( "$STAGE" == "validate" || "$STAGE" == "tacky" || "$STAGE" == "codegen" || "$STAGE" == "compile" ) ]]; then
        exp_exit=nonzero
    fi
    # sa/ tests: at validate+ stage, also compare stderr; at parse stage (and
    # at compile stage, since we can't know whether cc89's compile pipeline
    # has SA wired in yet) they should parse cleanly so exit 0 is correct.
    local sa_check_stderr=0
    if [[ "$kind" == "sa" && ( "$STAGE" == "validate" || "$STAGE" == "tacky" || "$STAGE" == "codegen" ) ]]; then
        exp_exit=nonzero
        sa_check_stderr=1
    fi

    local stderr_file
    stderr_file="$(mktemp -t cc89-stderr.XXXXXX)"
    if [[ -n "$STAGE_FLAG" ]]; then
        timeout "$TIMEOUT" "$CC89_BIN" "$STAGE_FLAG" "$f" >/dev/null 2>"$stderr_file"
    else
        timeout "$TIMEOUT" "$CC89_BIN" "$f" >/dev/null 2>"$stderr_file"
    fi
    got_exit=$?
    # exit 124 from `timeout` = the test hung; treat as failure with marker
    local timed_out=0
    [[ $got_exit -eq 124 ]] && timed_out=1

    local ok=1
    if [[ $timed_out -eq 1 ]]; then
        ok=0  # always count timeouts as failures, even for parse_invalid
    elif [[ "$exp_exit" == "nonzero" ]]; then
        [[ $got_exit -ne 0 ]] || ok=0
    else
        [[ $got_exit -eq $exp_exit ]] || ok=0
    fi

    if [[ $ok -eq 1 && $sa_check_stderr -eq 1 ]]; then
        local expected="${f%/*}/expected"
        if [[ -f "$expected" ]]; then
            # cc89 prints whatever path it was invoked with in diagnostics, but
            # `expected` files store bare paths like `benchmarks/sa/.../foo.c`.
            # Strip any prefix up to the last `/benchmarks/` so the diff isn't
            # defeated by cwd differences (./, /mnt/c/..., C:/...).
            sed -i -E 's|^.*/benchmarks/|benchmarks/|' "$stderr_file"
            # `expected` files committed with CRLF on Windows; cc89 emits LF.
            if ! diff -q --strip-trailing-cr "$stderr_file" "$expected" >/dev/null 2>&1; then
                ok=0
            fi
        fi
    fi

    rm -f "$stderr_file"

    if [[ $ok -eq 1 ]]; then
        PASS=$((PASS+1))
        [[ $VERBOSE -eq 1 ]] && echo "PASS $f"
    else
        FAIL=$((FAIL+1))
        local marker="exit=$got_exit"
        [[ $timed_out -eq 1 ]] && marker="TIMEOUT"
        FAILED_FILES+=("$f (kind=$kind $marker expected=$exp_exit)")
        [[ $VERBOSE -eq 1 ]] && echo "FAIL $f ($marker, expected $exp_exit)"
    fi
}

for f in "${TEST_FILES[@]}"; do
    # nlsandler tests live under chapter_N/ dirs and introduce features chapter by
    # chapter. Skip any whose chapter isn't in CHAPTERS_IMPLEMENTED at EVERY stage —
    # later chapters use long/double/structs/etc. the lexer/parser can't handle yet,
    # so even parsing/validating them would spuriously fail. Tests outside a chapter
    # dir (sa/, top-level benchmarks) are never gated this way.
    if [[ "$f" == *"/chapter_"* ]] && ! is_chapter_implemented "$f"; then
        SKIP=$((SKIP+1))
        continue
    fi
    # validate_invalid tests target semantic analysis; they only get a verdict
    # once SA runs. Don't run them at the lex/parse stages — cc89 may legitimately
    # reject some at parse time (e.g. break-outside-loop), which isn't a failure.
    if [[ ( "$STAGE" == "lex" || "$STAGE" == "parse" ) && "$(classify "$f")" == "validate_invalid" ]]; then
        SKIP=$((SKIP+1))
        continue
    fi
    if [[ "$STAGE" == "run" ]]; then
        if [[ "$(classify "$f")" != "parse_valid" ]]; then
            SKIP=$((SKIP+1))
            continue
        fi
        # stack_alignment.c needs the platform helper that defines
        # even_arguments/odd_arguments. Link against it if present; skip only if
        # the helper is missing (so the test can't link standalone).
        if [[ "$f" == *stack_alignment.c ]]; then
            helper="$(dirname "$f")/stack_alignment_check_linux.s"
            if [[ -f "$helper" ]]; then
                run_one_with_helper "$f" "$helper"
            else
                SKIP=$((SKIP+1))
            fi
            continue
        fi
        if [[ "$f" == *"/libraries/"* ]]; then
            run_one_library "$f"
        else
            run_one_execute "$f"
        fi
    else
        run_one "$f"
    fi
done

# ----- report -----------------------------------------------------------------
TOTAL=$((PASS+FAIL))
echo ""
echo "stage:  $STAGE"
echo "total:  $TOTAL"
echo "passed: $PASS"
echo "failed: $FAIL"
if [[ $SKIP -gt 0 ]]; then
    echo "skipped: $SKIP"
fi

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "failures:"
    for line in "${FAILED_FILES[@]}"; do
        echo "  $line"
    done
    exit 1
fi
