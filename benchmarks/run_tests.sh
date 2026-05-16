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
    lex|parse|validate|tacky|codegen|compile) ;;
    *) echo "unknown stage: $STAGE" >&2; exit 2 ;;
esac

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

if [[ ${#TEST_FILES[@]} -eq 0 ]]; then
    echo "no test files matched"; exit 1
fi

# ----- run --------------------------------------------------------------------
PASS=0
FAIL=0
FAILED_FILES=()

run_one() {
    local f="$1"
    local kind exp_exit got_exit got_err
    kind="$(classify "$f")"

    # default expectations: exit 0
    exp_exit=0

    if [[ "$kind" == "parse_invalid" ]]; then
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
            if ! diff -q "$stderr_file" "$expected" >/dev/null 2>&1; then
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
    run_one "$f"
done

# ----- report -----------------------------------------------------------------
TOTAL=$((PASS+FAIL))
echo ""
echo "stage:  $STAGE"
echo "total:  $TOTAL"
echo "passed: $PASS"
echo "failed: $FAIL"

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "failures:"
    for line in "${FAILED_FILES[@]}"; do
        echo "  $line"
    done
    exit 1
fi
