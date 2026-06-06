#!/usr/bin/env python3
"""tt.py - the cc89 test runner.

One entry point for building cc89 and exercising it over the benchmark suite at
any pipeline stage. Designed to run inside the Docker image (where gcc lives) but
also natively on Linux (CI) and on Windows for the non-`run` stages.

Usage:
    tt.py build
    tt.py <stage> [target] [options]      stage = lex|parse|validate|tacky|codegen|run
    tt.py asm <file.c> [--save]
    tt.py show <stage> <file.c>           print raw stage output (tokens/AST/asm),
                                          or for `run`, the program's own stdout

    target = (none)        all implemented mainline chapters
             9 | chapter_9 one chapter
             1-9           inclusive chapter range
             all           every chapter on disk (bypasses the implemented gate)
             <path>        a .c file or a directory (bypasses the gate)

    options:
      --extra-credit   also sweep benchmarks/extra_credit/ for the selected chapters
      -v, --verbose    print PASS/FAIL per file
      --timeout N      per-process timeout in seconds (default 5)
      --no-build       skip the implicit build before a stage sweep
      --cc89 PATH      use this cc89 binary instead of building/locating one
      --build-dir DIR  cmake build directory (default build-docker)
      --jobs N         build parallelism (default: os.cpu_count())

Exit codes: 0 all passed, 1 any failure, 2 argument/environment error.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
NLSANDLER = REPO_ROOT / "benchmarks" / "nlsandler"
EXTRA_CREDIT = REPO_ROOT / "benchmarks" / "extra_credit"
CATEGORIES = ("parse_valid", "parse_invalid", "validate_invalid")

# Chapters whose codegen is complete enough to run end-to-end. Bump as chapters
# land. This is the single source of truth for the implemented gate.
IMPLEMENTED_CHAPTERS = set(range(1, 13))  # chapters 1..12

STAGES = ("lex", "parse", "validate", "tacky", "codegen", "run")
_CHAP_RE = re.compile(r"chapter_(\d+)")


# --------------------------------------------------------------------------- #
# small helpers
# --------------------------------------------------------------------------- #
def die(msg: str, code: int = 2) -> "NoReturn":  # type: ignore[name-defined]
    print(f"tt: {msg}", file=sys.stderr)
    sys.exit(code)


def chapter_of(path: Path) -> int | None:
    m = _CHAP_RE.search(path.as_posix())
    return int(m.group(1)) if m else None


def classify(path: Path) -> str | None:
    """valid | invalid | validate_invalid, by which category dir the path is under."""
    parts = set(path.as_posix().split("/"))
    if "parse_valid" in parts:
        return "valid"
    if "parse_invalid" in parts:
        return "invalid"
    if "validate_invalid" in parts:
        return "validate_invalid"
    return None


def artifact_base(f: Path) -> str:
    """Collision-free artifact stem derived from the repo-relative path."""
    try:
        rel = f.relative_to(REPO_ROOT)
    except ValueError:
        rel = f
    return rel.as_posix().replace("/", "_")[:-2]  # drop trailing ".c"


def library_sibling(f: Path) -> Path:
    """Pair rule for library tests: X_client.c <-> X.c in the same directory."""
    stem = f.stem
    if stem.endswith("_client"):
        return f.with_name(stem[: -len("_client")] + ".c")
    return f.with_name(stem + "_client.c")


def run_proc(cmd, timeout, stdout=None, stderr=subprocess.DEVNULL):
    """Return (returncode, timed_out). returncode is None on timeout."""
    try:
        p = subprocess.run(cmd, stdout=stdout, stderr=stderr, timeout=timeout)
        return p.returncode, False
    except subprocess.TimeoutExpired:
        return None, True


# --------------------------------------------------------------------------- #
# build / locate cc89
# --------------------------------------------------------------------------- #
def build_dir(args) -> Path:
    return REPO_ROOT / (args.build_dir or os.environ.get("CC89_BUILD_DIR", "build-docker"))


def build_cc89(args, targets=("cc89",)) -> None:
    bd = build_dir(args)
    jobs = str(args.jobs or os.cpu_count() or 1)
    subprocess.run(["cmake", "-S", str(REPO_ROOT), "-B", str(bd)], check=True)
    subprocess.run(
        ["cmake", "--build", str(bd), "--target", *targets, "-j", jobs], check=True
    )


def locate_cc89(args) -> Path:
    if args.cc89:
        p = Path(args.cc89)
        return p if p.is_absolute() else (REPO_ROOT / p)
    env = os.environ.get("CC89")
    if env:
        return Path(env)
    for cand in (
        build_dir(args) / "cc89",
        REPO_ROOT / "cmake-build-debug" / "cc89",
        REPO_ROOT / "cmake-build-debug" / "cc89.exe",
        REPO_ROOT / "build" / "cc89",
    ):
        if cand.exists():
            return cand
    return build_dir(args) / "cc89"


def ensure_built(args) -> Path:
    if not args.no_build and not args.cc89:
        build_cc89(args)
    cc89 = locate_cc89(args)
    if not cc89.exists():
        die(f"cc89 binary not found at {cc89} (build first or pass --cc89)")
    return cc89


# --------------------------------------------------------------------------- #
# discovery
# --------------------------------------------------------------------------- #
def parse_target(target: str | None):
    """Return ('chapters', set|None) or ('path', Path). None set = implemented gate."""
    if target is None:
        return ("chapters", set(IMPLEMENTED_CHAPTERS))
    if target == "all":
        return ("chapters", None)  # None => every chapter on disk
    m = re.fullmatch(r"(?:chapter_)?(\d+)", target)
    if m:
        return ("chapters", {int(m.group(1))})
    m = re.fullmatch(r"(\d+)-(\d+)", target)
    if m:
        lo, hi = int(m.group(1)), int(m.group(2))
        return ("chapters", set(range(lo, hi + 1)))
    p = (REPO_ROOT / target) if not Path(target).is_absolute() else Path(target)
    if not p.exists():
        die(f"target not found: {target}")
    return ("path", p)


def categories_for_stage(stage: str):
    # Only valid-category programs are executable, so the run sweep ignores the rest.
    return ("parse_valid",) if stage == "run" else CATEGORIES


def category_dir(path: Path) -> str:
    """Human label for the group header, e.g. parse_valid or extra_credit/parse_invalid."""
    parts = path.as_posix().split("/")
    ec = "extra_credit/" if "extra_credit" in parts else ""
    for c in ("parse_valid", "parse_invalid", "validate_invalid"):
        if c in parts:
            return ec + c
    return "other"


def _sort_key(f: Path):
    # Group by chapter (numeric, un-chaptered last), then category, then path.
    c = chapter_of(f)
    return (c if c is not None else 10**9, category_dir(f), f.as_posix())


def discover(stage: str, target: str | None, extra_credit: bool):
    kind, val = parse_target(target)
    cats = categories_for_stage(stage)
    if kind == "path":
        p: Path = val
        files = [p] if p.is_file() else list(p.rglob("*.c"))
        return sorted(files, key=_sort_key)
    chapters = val  # set or None(all)
    roots = [NLSANDLER] + ([EXTRA_CREDIT] if extra_credit else [])
    files = []
    for root in roots:
        for cat in cats:
            base = root / cat
            if not base.exists():
                continue
            for f in base.rglob("*.c"):
                n = chapter_of(f)
                if chapters is None or (n is not None and n in chapters):
                    files.append(f)
    return sorted(set(files), key=_sort_key)


# --------------------------------------------------------------------------- #
# expectations
# --------------------------------------------------------------------------- #
def expectation(stage: str, cls: str) -> str:
    """expect0 | expectnz | execute | skip"""
    if stage == "run":
        return "execute" if cls == "valid" else "skip"
    if cls == "valid":
        return "expect0"
    if cls == "invalid":
        return "expectnz"
    # validate_invalid: clean at lex/parse, rejected at validate+
    return "skip" if stage in ("lex", "parse") else "expectnz"


# --------------------------------------------------------------------------- #
# run-stage execution (single / library pair / stack_alignment)
# --------------------------------------------------------------------------- #
def execute(f: Path, cc89: Path, art: Path, timeout: float):
    """Return (ok, reason). Compares our exit code against a gcc reference build."""
    base = artifact_base(f)
    asm = art / f"{base}.s"

    # Tests that link against a committed platform helper (.s defining symbols
    # the C file declares extern).
    helper_fixtures = {
        "stack_alignment.c": "stack_alignment_check_linux.s",
        "push_arg_on_page_boundary.c": "data_on_page_boundary_linux.s",
    }
    if f.name in helper_fixtures:
        helper = f.parent / helper_fixtures[f.name]
        if helper.exists():
            return _exec_linked(f, cc89, art, timeout, extra=[str(helper)])

    if "/libraries/" in f.as_posix():
        sib = library_sibling(f)
        if sib.exists():
            return _exec_pair(f, sib, cc89, art, timeout)

    # plain single-file program
    our, ref = art / f"{base}.our", art / f"{base}.ref"
    with open(asm, "wb") as out:
        rc, to = run_proc([str(cc89), "--codegen", str(f)], timeout, stdout=out)
    if to:
        return False, "cc89 --codegen TIMEOUT"
    if rc != 0:
        return False, f"cc89 --codegen failed (exit {rc})"
    if run_proc(["gcc", "-w", str(asm), "-o", str(our)], timeout)[0] != 0:
        return False, "gcc could not assemble/link cc89 output"
    if run_proc(["gcc", "-w", str(f), "-o", str(ref)], timeout)[0] != 0:
        return False, "reference gcc build failed"
    return _compare(our, ref, timeout)


def _exec_linked(f, cc89, art, timeout, extra):
    base = artifact_base(f)
    asm, our, ref = art / f"{base}.s", art / f"{base}.our", art / f"{base}.ref"
    with open(asm, "wb") as out:
        rc, to = run_proc([str(cc89), "--codegen", str(f)], timeout, stdout=out)
    if to or rc != 0:
        return False, ("cc89 --codegen TIMEOUT" if to else f"cc89 --codegen failed (exit {rc})")
    if run_proc(["gcc", "-w", str(asm), *extra, "-o", str(our)], timeout)[0] != 0:
        return False, "gcc could not assemble/link cc89 output"
    if run_proc(["gcc", "-w", str(f), *extra, "-o", str(ref)], timeout)[0] != 0:
        return False, "reference gcc build failed"
    return _compare(our, ref, timeout)


def _exec_pair(f, sib, cc89, art, timeout):
    """Build file-under-test with cc89, its sibling with gcc, link, compare to all-gcc."""
    base = artifact_base(f)
    asm = art / f"{base}.s"
    our_o, sib_o = art / f"{base}.o", art / f"{base}.sib.o"
    our, ref = art / f"{base}.our", art / f"{base}.ref"
    with open(asm, "wb") as out:
        rc, to = run_proc([str(cc89), "--codegen", str(f)], timeout, stdout=out)
    if to or rc != 0:
        return False, ("cc89 --codegen TIMEOUT" if to else f"cc89 --codegen failed (exit {rc})")
    if run_proc(["gcc", "-w", "-c", str(asm), "-o", str(our_o)], timeout)[0] != 0:
        return False, "gcc could not assemble cc89 output"
    if run_proc(["gcc", "-w", "-c", str(sib), "-o", str(sib_o)], timeout)[0] != 0:
        return False, "gcc could not compile sibling"
    if run_proc(["gcc", "-w", str(our_o), str(sib_o), "-o", str(our)], timeout)[0] != 0:
        return False, "link of cc89+gcc objects failed"
    if run_proc(["gcc", "-w", str(f), str(sib), "-o", str(ref)], timeout)[0] != 0:
        return False, "reference gcc build failed"
    return _compare(our, ref, timeout)


def _compare(our: Path, ref: Path, timeout: float):
    # Suppress the test program's own stdout so it doesn't pollute the report.
    oe, oto = run_proc([str(our)], timeout, stdout=subprocess.DEVNULL)
    re_, rto = run_proc([str(ref)], timeout, stdout=subprocess.DEVNULL)
    if oto or rto:
        return False, "TIMEOUT running program"
    if oe != re_:
        return False, f"exit mismatch: cc89={oe} gcc={re_}"
    return True, ""


# --------------------------------------------------------------------------- #
# commands
# --------------------------------------------------------------------------- #
def cmd_build(args) -> int:
    targets = ("cc89", "test_lexer")
    build_cc89(args, targets)
    print(f"built {', '.join(targets)} in {build_dir(args)}")
    return 0


def cmd_asm(args) -> int:
    cc89 = ensure_built(args)
    f = (REPO_ROOT / args.file) if not Path(args.file).is_absolute() else Path(args.file)
    if not f.exists():
        die(f"file not found: {args.file}")
    proc = subprocess.run([str(cc89), "--codegen", str(f)], capture_output=True, text=True)
    sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    if args.save:
        art = build_dir(args) / "test-artifacts"
        art.mkdir(parents=True, exist_ok=True)
        out = art / f"{artifact_base(f)}.s"
        out.write_text(proc.stdout)
        print(f"\n[saved {out}]", file=sys.stderr)
    print(f"[cc89 exit {proc.returncode}]", file=sys.stderr)
    return proc.returncode


def cmd_show(args) -> int:
    """Run one file through a stage and print the raw output (no pass/fail judging).

    For `run`, the program is compiled, linked, and executed with its stdout/stderr
    streamed straight to the terminal so you can see what it prints.
    """
    cc89 = ensure_built(args)
    f = (REPO_ROOT / args.file) if not Path(args.file).is_absolute() else Path(args.file)
    if not f.exists():
        die(f"file not found: {args.file}")
    stage = args.stage

    if stage == "run":
        if shutil.which("gcc") is None:
            die("'show run' needs gcc on PATH (use the Docker image or a Linux shell)")
        art = build_dir(args) / "test-artifacts"
        art.mkdir(parents=True, exist_ok=True)
        base = artifact_base(f)
        asm, exe = art / f"{base}.s", art / f"{base}.out"
        with open(asm, "wb") as out:
            cg = subprocess.run([str(cc89), "--codegen", str(f)], stdout=out, stderr=subprocess.PIPE)
        if cg.returncode != 0:
            sys.stderr.buffer.write(cg.stderr or b"")
            die(f"cc89 --codegen failed (exit {cg.returncode})", cg.returncode)
        link = subprocess.run(["gcc", "-w", str(asm), "-o", str(exe)], capture_output=True)
        if link.returncode != 0:
            sys.stderr.buffer.write(link.stderr or b"")
            die("gcc could not assemble/link cc89 output", 1)
        # Inherit stdio so the program's output streams directly to the terminal.
        proc = subprocess.run([str(exe)])
        print(f"\n[program exit {proc.returncode}]", file=sys.stderr)
        return proc.returncode

    # Non-run stages just print what cc89 emits at that stage. parse/validate need
    # --debugAST and tacky needs --debugTacky to actually dump anything.
    flags = {"parse": ["--debugAST"], "validate": ["--debugAST"], "tacky": ["--debugTacky"]}
    proc = subprocess.run(
        [str(cc89), f"--{stage}", *flags.get(stage, []), str(f)], capture_output=True, text=True
    )
    sys.stdout.write(proc.stdout)
    if proc.stderr:
        sys.stderr.write(proc.stderr)
    print(f"[cc89 exit {proc.returncode}]", file=sys.stderr)
    return proc.returncode


def cmd_stage(args) -> int:
    stage = args.command
    if stage == "run" and shutil.which("gcc") is None:
        die("stage 'run' needs gcc on PATH (use the Docker image or a Linux shell)")

    cc89 = ensure_built(args)
    files = discover(stage, args.target, args.extra_credit)
    if not files:
        die("no test files matched", 1)

    art = build_dir(args) / "test-artifacts"
    if stage == "run":
        art.mkdir(parents=True, exist_ok=True)

    ec = "on" if args.extra_credit else "off"
    print(f"stage: {stage}   (extra-credit: {ec})", flush=True)

    npass = nfail = nskip = 0
    cur_group = None  # (chapter, category) of the header last printed

    def emit(f, status):
        # Stream a "category / chapter_N:" header whenever the group changes,
        # then just the file name + status (the path context is in the header).
        nonlocal cur_group
        ch, cat = chapter_of(f), category_dir(f)
        group = (ch, cat)
        if group != cur_group:
            head = f"{cat} / chapter_{ch}:" if ch is not None else f"{cat}:"
            print(("\n" if cur_group is not None else "") + head, flush=True)
            cur_group = group
        print(f"  {f.name:<46}  {status}", flush=True)

    for f in files:
        cls = classify(f)
        if cls is None:
            continue
        act = expectation(stage, cls)

        if act == "skip":
            nskip += 1
            if args.verbose:
                emit(f, "skip")
            continue

        if act == "execute":
            ok, reason = execute(f, cc89, art, args.timeout)
        else:
            rc, to = run_proc([str(cc89), f"--{stage}", str(f)], args.timeout)
            if to:
                ok, reason = False, "TIMEOUT"
            elif act == "expect0":
                ok, reason = (rc == 0), f"exit {rc}, expected 0"
            else:  # expectnz
                ok, reason = (rc != 0), f"exit {rc}, expected non-zero"

        if ok:
            npass += 1
            emit(f, "pass")
        else:
            nfail += 1
            emit(f, f"FAIL  {reason}")

    print(f"\nstage: {stage}   (extra-credit: {ec})")
    print(f"PASS: {npass}   FAIL: {nfail}   SKIP: {nskip}")
    return 1 if nfail else 0


# --------------------------------------------------------------------------- #
# argument parsing
# --------------------------------------------------------------------------- #
def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(prog="tt.py", description="cc89 test runner")
    sub = p.add_subparsers(dest="command", required=True)

    def add_common(sp):
        sp.add_argument("--cc89", help="path to cc89 binary")
        sp.add_argument("--build-dir", help="cmake build directory (default build-docker)")
        sp.add_argument("--jobs", type=int, help="build parallelism")

    b = sub.add_parser("build", help="build cc89 + test_lexer")
    add_common(b)
    b.add_argument("--no-build", action="store_true", help=argparse.SUPPRESS)

    a = sub.add_parser("asm", help="emit cc89 assembly for one file")
    a.add_argument("file")
    a.add_argument("--save", action="store_true", help="also write the .s to artifacts")
    a.add_argument("--no-build", action="store_true")
    add_common(a)

    s = sub.add_parser("show", help="run one file through a stage and print its raw output")
    s.add_argument("stage", choices=STAGES, help="lex|parse|validate|tacky|codegen|run")
    s.add_argument("file")
    s.add_argument("--no-build", action="store_true")
    add_common(s)

    for stage in STAGES:
        sp = sub.add_parser(stage, help=f"run the {stage} stage over selected tests")
        sp.add_argument("target", nargs="?", help="chapter | range | all | path")
        sp.add_argument("--extra-credit", action="store_true")
        sp.add_argument("-v", "--verbose", action="store_true")
        sp.add_argument("--timeout", type=float, default=5.0)
        sp.add_argument("--no-build", action="store_true")
        add_common(sp)
    return p


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    if args.command == "build":
        return cmd_build(args)
    if args.command == "asm":
        return cmd_asm(args)
    if args.command == "show":
        return cmd_show(args)
    return cmd_stage(args)


if __name__ == "__main__":
    sys.exit(main())
