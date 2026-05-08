#!/usr/bin/env python3
"""Import nlsandler/writing-a-c-compiler-tests into benchmarks/nlsandler/.

Filters out tests that depend on C features cc89 doesn't support
(long, double, unsigned, static, extern, goto, switch, enum, typedef, ...).
Adds braces around brace-less if/while/for/else/do bodies, since cc89 requires
braced bodies. Tests whose entire point IS brace-less form (e.g. null_body)
are skipped.

The script is idempotent: it overwrites everything under
benchmarks/nlsandler/{parse_valid,parse_invalid}/ on every run.
"""

import os
import re
import shutil
import sys
from pathlib import Path

SRC_ROOT_CANDIDATES = [
    Path(os.environ.get("TEMP", "/tmp")) / "wacc-tests" / "tests",
    Path("/tmp/wacc-tests/tests"),
]
SRC_ROOT = next((p for p in SRC_ROOT_CANDIDATES if p.exists()), SRC_ROOT_CANDIDATES[0])
DST_ROOT = Path(__file__).parent

SKIP_CHAPTERS = {11, 12, 13, 17, 19, 20}

UNSUPPORTED_TOKENS = [
    r"\blong\b", r"\bdouble\b", r"\bfloat\b",
    r"\bunsigned\b", r"\bsigned\b", r"\bshort\b",
    r"\bstatic\b", r"\bextern\b", r"\bregister\b", r"\bauto\b",
    r"\bgoto\b", r"\bswitch\b", r"\bcase\b", r"\bdefault\s*:",
    r"\benum\b", r"\bunion\b", r"\btypedef\b", r"\bconst\b", r"\bvolatile\b",
    r"\binline\b", r"\brestrict\b",
    r"\b_Bool\b", r"\bbool\b", r"\b_Static_assert\b",
    r"\bsize_t\b", r"\bint64_t\b", r"\bint32_t\b", r"\buint\w+\b",
    r"//",                     # // comments are C99
    r"^\s*#",                  # any preprocessor directive — cc89 has no PP
]
UNSUPPORTED_RE = re.compile("|".join(UNSUPPORTED_TOKENS), re.MULTILINE)

# C99 declaration in for-init: `for (int i = ...; ...; ...)`. cc89's parser
# expects an expression statement here, so these fail at parse time.
FOR_DECL_RE = re.compile(r"\bfor\s*\(\s*(?:int|char|void|struct)\b")

# filenames whose whole point is the brace-less form — drop them
SKIP_FILENAMES = {
    "if_null_body.c",
    "null_body.c",
    "empty_body.c",
    "do_while_empty_body.c",
    "for_empty_body.c",
    "while_empty_body.c",
}


def strip_strings_comments(src: str):
    """Replace string/char literals and comments with spaces (preserve length).
    Lets the brace-finder operate without worrying about lexical inner content."""
    out = list(src)
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            quote = c
            j = i + 1
            while j < n and src[j] != quote:
                if src[j] == "\\" and j + 1 < n:
                    out[j] = " "
                    out[j + 1] = " "
                    j += 2
                    continue
                out[j] = " "
                j += 1
            if j < n:
                out[j] = " "
                j += 1
            out[i] = " "
            i = j
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "/":
            while i < n and src[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if c == "/" and i + 1 < n and src[i + 1] == "*":
            j = i + 2
            while j + 1 < n and not (src[j] == "*" and src[j + 1] == "/"):
                out[j] = " "
                j += 1
            for k in range(i, min(j + 2, n)):
                out[k] = " "
            i = j + 2
            continue
        i += 1
    return "".join(out)


def find_matching(src: str, open_idx: int, open_ch: str, close_ch: str) -> int:
    """Return index of the closing character matching src[open_idx]."""
    depth = 1
    i = open_idx + 1
    while i < len(src) and depth > 0:
        if src[i] == open_ch:
            depth += 1
        elif src[i] == close_ch:
            depth -= 1
        i += 1
    return i - 1 if depth == 0 else -1


def add_braces(src: str) -> str:
    """Wrap brace-less if/while/for/else/do bodies in `{ ... }`."""
    masked = strip_strings_comments(src)

    # collect insertion points: list of (offset, text_to_insert)
    inserts = []  # each: (idx, "{") and (idx, "}")

    # Scan for keyword bodies. Use the masked string for searching, but inserts
    # apply to original.
    pos = 0
    pending_dos = []  # stack of `do` body-end indices to remember the `while`-tail context

    # We do this in two phases: first identify bodies, then mutate.
    # For each control-flow construct, body is either '{...}' or a statement
    # ending with the next ';' at depth 0 of () and {}.
    #
    # We walk linearly. When we see a keyword, we determine the body range.

    def skip_ws(s, i):
        while i < len(s) and s[i] in " \t\n\r":
            i += 1
        return i

    def find_body_end(s, body_start):
        """Find end offset (exclusive) of a brace-less statement body starting at body_start.
        Body ends at the first ';' encountered at depth 0 of (), {} and after fully
        consuming any nested if/while/for/do constructs (which themselves end at ;)."""
        i = body_start
        n = len(s)
        # body could start with another control flow keyword — recurse implicitly
        # by scanning to the matching ';' at depth 0.
        depth_p = 0
        depth_b = 0
        while i < n:
            c = s[i]
            if c == "(":
                depth_p += 1
            elif c == ")":
                depth_p -= 1
            elif c == "{":
                depth_b += 1
            elif c == "}":
                depth_b -= 1
            elif c == ";" and depth_p == 0 and depth_b == 0:
                return i + 1
            i += 1
        return n

    def find_if_end(s, kw_start):
        """Index past the entire if-elif-else chain starting at `if` keyword."""
        n = len(s)
        i = kw_start + 2  # past 'if'
        i = skip_ws(s, i)
        if i >= n or s[i] != "(":
            return n
        cp = find_matching(s, i, "(", ")")
        if cp < 0:
            return n
        i = skip_ws(s, cp + 1)
        if i < n and s[i] == "{":
            i = find_matching(s, i, "{", "}") + 1
        else:
            i = find_body_end(s, i)
        # consume any chain of `else [if ...]`
        while True:
            j = skip_ws(s, i)
            if j + 4 <= n and s[j:j+4] == "else" and (j+4 == n or not (s[j+4].isalnum() or s[j+4] == "_")):
                i = skip_ws(s, j + 4)
                if i + 2 <= n and s[i:i+2] == "if" and (i+2 == n or not (s[i+2].isalnum() or s[i+2] == "_")):
                    i = find_if_end(s, i)
                elif i < n and s[i] == "{":
                    i = find_matching(s, i, "{", "}") + 1
                else:
                    i = find_body_end(s, i)
            else:
                break
        return i

    # Find keywords in masked, but track do/while pairing.
    KW_RE = re.compile(r"\b(if|else|while|for|do)\b")
    i = 0
    do_stack = []  # indices of `do` whose `while` we still need to skip
    while True:
        m = KW_RE.search(masked, i)
        if not m:
            break
        kw = m.group(1)
        kw_start = m.start()
        kw_end = m.end()
        i = kw_end

        if kw == "do":
            # body of do
            j = skip_ws(masked, kw_end)
            if j < len(masked) and masked[j] == "{":
                # already braced — find matching }
                close = find_matching(masked, j, "{", "}")
                # the matching `while` follows close — mark to skip it
                do_stack.append(close + 1)
            else:
                end = find_body_end(masked, j)
                inserts.append((j, "{ "))
                inserts.append((end, " }"))
                do_stack.append(end)
            continue

        if kw == "while":
            # is this the tail of a do-while? if so, skip its `( ... ) ;`
            if do_stack:
                do_end = do_stack[-1]
                # only treat as do-while-tail if no intervening control-flow keyword
                # (i.e., `while` immediately follows the do-body, ignoring whitespace)
                between = masked[do_end:kw_start].strip()
                if between == "":
                    do_stack.pop()
                    # advance past the `( ... ) ;`
                    p = skip_ws(masked, kw_end)
                    if p < len(masked) and masked[p] == "(":
                        cp = find_matching(masked, p, "(", ")")
                        i = cp + 1
                    continue
            # plain while — fallthrough

        # if / while / for / else
        if kw in ("if", "while", "for"):
            p = skip_ws(masked, kw_end)
            if p >= len(masked) or masked[p] != "(":
                continue
            cp = find_matching(masked, p, "(", ")")
            if cp < 0:
                continue
            body_start = skip_ws(masked, cp + 1)
            i = cp + 1
        elif kw == "else":
            body_start = skip_ws(masked, kw_end)
            i = kw_end
        else:
            continue

        if body_start >= len(masked):
            continue
        if masked[body_start] == "{":
            continue  # already braced
        if kw == "else" and re.match(r"\bif\b", masked[body_start:]):
            # `else if (...)` — wrap the entire inner if-chain so the parser
            # sees `else { if (...) ... }` (cc89's parseIfStmt requires `{`
            # after `else`).
            end = find_if_end(masked, body_start)
            inserts.append((body_start, "{ "))
            inserts.append((end, " }"))
            continue

        end = find_body_end(masked, body_start)
        inserts.append((body_start, "{ "))
        inserts.append((end, " }"))

    if not inserts:
        return src

    # Apply inserts in reverse order to preserve offsets
    inserts.sort(key=lambda x: x[0], reverse=True)
    out = src
    for idx, txt in inserts:
        out = out[:idx] + txt + out[idx:]
    return out


def chapter_num(path: Path):
    for part in path.parts:
        m = re.match(r"chapter_(\d+)$", part)
        if m:
            return int(m.group(1))
    return None


def is_unsupported(text: str) -> bool:
    return bool(UNSUPPORTED_RE.search(text))


def main():
    if not SRC_ROOT.exists():
        sys.exit(f"missing {SRC_ROOT} — clone nlsandler/writing-a-c-compiler-tests there first")

    parse_valid = DST_ROOT / "parse_valid"
    parse_invalid = DST_ROOT / "parse_invalid"
    for d in (parse_valid, parse_invalid):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    stats = {"imported_valid": 0, "imported_invalid": 0,
             "skipped_unsupported": 0, "skipped_filename": 0,
             "skipped_chapter": 0}

    for src_file in sorted(SRC_ROOT.rglob("*.c")):
        ch = chapter_num(src_file)
        if ch is None or ch in SKIP_CHAPTERS:
            stats["skipped_chapter"] += 1
            continue

        rel = src_file.relative_to(SRC_ROOT)
        parts = rel.parts
        # parts[0] = chapter_N, parts[1] = valid/invalid_*, parts[2:] = subpath, name
        if len(parts) < 3:
            continue
        bucket = parts[1]

        if bucket == "valid":
            dst_root = parse_valid
        elif bucket == "invalid_parse":
            dst_root = parse_invalid
        elif bucket == "invalid_lex":
            dst_root = parse_invalid  # lexer errors also cause cc89 to exit non-zero
        else:
            # invalid_semantics, invalid_types, invalid_declarations, invalid_labels,
            # invalid_struct_tags — these are semantic errors, cc89 (parser-only)
            # currently lets them through. Skip until SA lands.
            continue

        if src_file.name in SKIP_FILENAMES:
            stats["skipped_filename"] += 1
            continue

        text = src_file.read_text(encoding="utf-8", errors="replace")
        if is_unsupported(text):
            stats["skipped_unsupported"] += 1
            continue
        # invalid_parse tests are allowed to use C99 for-init decls (they're
        # supposed to fail anyway). For valid tests, drop them — cc89 can't
        # parse `for (int i = 0; ...)`.
        if bucket == "valid" and FOR_DECL_RE.search(text):
            stats["skipped_unsupported"] += 1
            continue

        # only brace-add for valid tests — invalid_parse tests should be left
        # untouched so they still trigger the parse error they're meant to.
        if bucket == "valid":
            text = add_braces(text)

        # Drop the bucket name from the path: chapter_N/<sub...>/file.c
        flat_rel = Path(parts[0]) / Path(*parts[2:])
        dst_path = dst_root / flat_rel
        dst_path.parent.mkdir(parents=True, exist_ok=True)
        dst_path.write_text(text, encoding="utf-8")

        if bucket == "valid":
            stats["imported_valid"] += 1
        else:
            stats["imported_invalid"] += 1

    print(f"imported valid:        {stats['imported_valid']}")
    print(f"imported invalid:      {stats['imported_invalid']}")
    print(f"skipped (chapter):     {stats['skipped_chapter']}")
    print(f"skipped (unsupported): {stats['skipped_unsupported']}")
    print(f"skipped (filename):    {stats['skipped_filename']}")


if __name__ == "__main__":
    main()
