#!/usr/bin/env python3
"""One-shot: move MainWindow method bodies out-of-line (ISS-0004 Phase B).

Preserves Q_OBJECT / slots / nested type decls in the header. Move-only.
"""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HDR = ROOT / "include" / "MainWindow.h"
CPP = ROOT / "src" / "MainWindow.cpp"


def strip_strings_and_comments(line: str, in_block: bool, in_string: str | None) -> tuple[str, bool, str | None]:
    """Return a scrubbed line for brace counting (approx)."""
    out = []
    i = 0
    while i < len(line):
        ch = line[i]
        nxt = line[i + 1] if i + 1 < len(line) else ""
        if in_block:
            if ch == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if in_string:
            if ch == "\\" and i + 1 < len(line):
                i += 2
                continue
            if ch == in_string:
                in_string = None
            i += 1
            continue
        if ch == "/" and nxt == "/":
            break
        if ch == "/" and nxt == "*":
            in_block = True
            i += 2
            continue
        if ch in ('"', "'"):
            in_string = ch
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out), in_block, in_string


def find_matching_brace(lines: list[str], open_line: int, open_col: int) -> tuple[int, int]:
    """Return (line_idx, col) of matching '}' for '{' at open_line/open_col."""
    depth = 0
    in_block = False
    in_string: str | None = None
    for li in range(open_line, len(lines)):
        line = lines[li]
        start = open_col if li == open_line else 0
        # Process character by character with state
        i = start
        while i < len(line):
            ch = line[i]
            nxt = line[i + 1] if i + 1 < len(line) else ""
            if in_block:
                if ch == "*" and nxt == "/":
                    in_block = False
                    i += 2
                    continue
                i += 1
                continue
            if in_string:
                if ch == "\\" and i + 1 < len(line):
                    i += 2
                    continue
                if ch == in_string:
                    in_string = None
                i += 1
                continue
            if ch == "/" and nxt == "/":
                break
            if ch == "/" and nxt == "*":
                in_block = True
                i += 2
                continue
            if ch in ('"', "'"):
                in_string = ch
                i += 1
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return li, i
            i += 1
    raise RuntimeError(f"unbalanced brace starting at line {open_line + 1}")


FUNC_START_RE = re.compile(
    r"""^(?P<indent>\s*)
        (?:explicit\s+)?
        (?:(?:inline|virtual|static|constexpr|friend)\s+)*
        (?P<ret>(?:[\w:<>,\s\*&]+?))?
        (?P<name>~?\w+)\s*
        \(
    """,
    re.VERBOSE,
)


def is_likely_member_function_start(scrubbed: str) -> bool:
    s = scrubbed.strip()
    if not s:
        return False
    if s.startswith(("struct ", "class ", "enum ", "using ", "typedef ", "namespace ")):
        return False
    if s.startswith(("public:", "private:", "protected:", "signals:", "slots:")):
        return False
    if s.startswith("Q_OBJECT"):
        return False
    # data members like `int x = 0;` or `std::vector<Foo> bars;`
    if s.endswith(";") and "{" not in s:
        return False
    # Nested type with body starting on same line: struct Foo {
    if re.match(r"^(struct|class|enum)\b", s):
        return False
    # Constructor / destructor / method: has (
    if "(" not in s:
        return False
    # Skip pure data with paren in type? rare
    return True


def split_params_and_rest(sig: str) -> tuple[str, str, str, str]:
    """Return (name_and_ret, params_inner, quals, init_list).

    init_list includes leading ':' when present (ctor).
    """
    depth = 0
    end_params = -1
    in_str = None
    i = 0
    started = False
    brace = 0
    while i < len(sig):
        ch = sig[i]
        if in_str:
            if ch == "\\" and i + 1 < len(sig):
                i += 2
                continue
            if ch == in_str:
                in_str = None
            i += 1
            continue
        if ch in ('"', "'"):
            in_str = ch
            i += 1
            continue
        if ch == "{":
            brace += 1
        elif ch == "}":
            brace = max(0, brace - 1)
        elif ch == "(" and brace == 0:
            depth += 1
            started = True
        elif ch == ")" and brace == 0:
            depth -= 1
            if started and depth == 0:
                end_params = i
                break
        i += 1
    if end_params < 0:
        raise RuntimeError(f"cannot find param list in: {sig[:160]!r}")
    params_part = sig[: end_params + 1]
    rest = sig[end_params + 1 :]
    lp = params_part.find("(")
    name_and_ret = params_part[:lp]
    inner = params_part[lp + 1 : end_params - lp] if False else params_part[lp + 1 : -1]
    # Actually params_part ends with ')'; inner is between
    inner = params_part[lp + 1 : -1]

    # quals vs init list
    rest_lstrip = rest.lstrip(" \t")
    # Find ctor init list: newline + spaces + ':' or ' :'
    init = ""
    quals = rest
    # Prefer splitting on a line-leading ':' (ctor init), not ternary
    m = re.search(r"\n\s*:", rest)
    if m:
        quals = rest[: m.start()]
        init = rest[m.start() :].lstrip("\n")
        if not init.lstrip().startswith(":"):
            init = ":" + init.split(":", 1)[-1]
    elif rest_lstrip.startswith(":"):
        # same-line init (rare)
        quals = ""
        init = rest_lstrip
    return name_and_ret, inner, quals.strip(), init


def split_top_level_params(inner: str) -> list[str]:
    parts: list[str] = []
    buf: list[str] = []
    depth = 0
    brace = 0
    angle = 0
    in_str = None
    for j, ch in enumerate(inner):
        if in_str:
            buf.append(ch)
            if ch == "\\" and j + 1 < len(inner):
                continue
            if ch == in_str:
                in_str = None
            continue
        if ch in ('"', "'"):
            in_str = ch
            buf.append(ch)
            continue
        if ch == "<":
            angle += 1
        elif ch == ">" and angle:
            angle -= 1
        elif ch == "{":
            brace += 1
        elif ch == "}":
            brace = max(0, brace - 1)
        elif ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0 and brace == 0 and angle == 0:
            parts.append("".join(buf))
            buf = []
            continue
        buf.append(ch)
    if buf or inner.strip():
        parts.append("".join(buf))
    return parts


def strip_param_default(param: str) -> str:
    ddepth = 0
    dbrace = 0
    dangle = 0
    in_str = None
    cut = len(param)
    k = 0
    while k < len(param):
        c = param[k]
        if in_str:
            if c == "\\" and k + 1 < len(param):
                k += 2
                continue
            if c == in_str:
                in_str = None
            k += 1
            continue
        if c in ('"', "'"):
            in_str = c
            k += 1
            continue
        if c == "<":
            dangle += 1
        elif c == ">" and dangle:
            dangle -= 1
        elif c == "{":
            dbrace += 1
        elif c == "}":
            dbrace = max(0, dbrace - 1)
        elif c == "(":
            ddepth += 1
        elif c == ")":
            ddepth -= 1
        elif c == "=" and ddepth == 0 and dbrace == 0 and dangle == 0:
            cut = k
            break
        k += 1
    return param[:cut].rstrip()


def make_header_declaration(sig_text: str) -> str:
    """Keep defaults; drop ctor init list / body; end with ';'."""
    name_and_ret, inner, quals, _init = split_params_and_rest(sig_text.rstrip())
    decl = f"{name_and_ret}({inner})"
    if quals:
        decl += " " + quals
    decl = decl.rstrip()
    if not decl.endswith(";"):
        decl += ";"
    # Preserve original indentation of first line
    return decl


def build_out_of_line(decl_text: str, body_text: str, class_name: str = "MainWindow") -> str:
    """Convert in-class definition to out-of-line definition."""
    before_brace = decl_text.rstrip()
    name_and_ret, inner, quals, init = split_params_and_rest(before_brace)
    parts = split_top_level_params(inner)
    cleaned = [strip_param_default(p) for p in parts]
    m = re.search(r"(~?\b\w+)\s*$", name_and_ret.rstrip())
    if not m:
        raise RuntimeError(f"cannot find name in {name_and_ret!r}")
    name = m.group(1)
    prefix = name_and_ret[: m.start(1)]
    # Drop leading indent on first line
    prefix_lines = prefix.splitlines()
    if prefix_lines:
        prefix_lines[0] = prefix_lines[0].lstrip()
        prefix = "\n".join(prefix_lines)
    # Out-of-line definitions must not repeat explicit/virtual/static/inline/friend.
    prefix = re.sub(
        r"\b(explicit|virtual|static|inline|friend|constexpr)\b\s*",
        "",
        prefix,
    )
    out_sig = f"{prefix}{class_name}::{name}(" + ", ".join(cleaned) + ")"
    if quals:
        out_sig += " " + quals
    if init:
        # normalize init to start on next indented line
        init_s = init.strip()
        if not init_s.startswith(":"):
            init_s = ": " + init_s
        out_sig += "\n    " + init_s
    if not body_text.endswith("\n"):
        body_text += "\n"
    return out_sig + "\n" + body_text


def main() -> None:
    text = HDR.read_text(encoding="utf-8", errors="replace")
    lines = text.splitlines(keepends=True)

    # Locate class
    class_start = None
    for i, line in enumerate(lines):
        if re.match(r"^class MainWindow\b", line):
            class_start = i
            break
    if class_start is None:
        raise SystemExit("class MainWindow not found")

    # Find opening { of class
    class_open_line = None
    class_open_col = None
    for i in range(class_start, min(class_start + 5, len(lines))):
        if "{" in lines[i]:
            class_open_line = i
            class_open_col = lines[i].index("{")
            break
    if class_open_line is None:
        raise SystemExit("class opening brace not found")

    class_end_line, _ = find_matching_brace(lines, class_open_line, class_open_col)

    # Walk class body at depth tracking relative to class
    # Collect member function ranges: (decl_start_line, brace_line, brace_col, end_line, end_col)
    methods: list[tuple[int, int, int, int, int]] = []
    i = class_open_line + 1
    in_block = False
    in_string: str | None = None
    # relative depth: 0 = inside class at member level
    # We track absolute braces from class open, so member level is abs_depth==1
    abs_depth = 1  # already inside class
    decl_start: int | None = None
    pending_func = False

    while i < class_end_line:
        raw = lines[i]
        scrub, in_block, in_string = strip_strings_and_comments(raw, in_block, in_string)
        # If we're at member level and not mid-function, look for function starts
        if abs_depth == 1 and not in_block and in_string is None:
            if decl_start is None:
                if is_likely_member_function_start(scrub):
                    decl_start = i
                    pending_func = True
            if pending_func and decl_start is not None:
                # Look for { that starts the function body at depth transition
                # Count braces on this line; when we would open a body while at depth 1
                pass

        # Character scan for braces and function body detection
        j = 0
        line = raw
        # Re-scan with state local copy starting fresh-ish — use shared in_block/in_string
        # Reset per-line string state is wrong for multi-line strings; keep shared.
        # But strip_strings already advanced state; re-parse for brace positions carefully.
        # Simpler: re-walk chars with a duplicate state from BEFORE this line.
        # We already updated in_block/in_string via strip — redo properly:

        # Actually redo line parse for braces with state carried from previous lines.
        # Restore state before strip by re-parsing from class start is expensive.
        # Use the scrubbed line for brace counts at member level — comments/strings removed.
        if not in_block:
            # Use scrub for braces only when not mid-block-comment spanning
            pass

        # Proper approach: maintain state and find `{` openings for functions
        # Reset: re-read state incorrectly. Let's maintain char state across lines properly
        # by not using strip's updated state for the next iteration incorrectly.

        # FIX: parse line once for both state and braces
        i += 1

    # Restart with cleaner single-pass algorithm
    methods = []
    i = class_open_line + 1
    in_block = False
    in_string = None
    abs_depth = 1
    decl_start = None

    while i < class_end_line:
        line = lines[i]
        # Snapshot depth at start of line
        depth_at_line_start = abs_depth

        if depth_at_line_start == 1 and decl_start is None and not in_block and in_string is None:
            scrub_preview, _, _ = strip_strings_and_comments(line, False, None)
            # Also allow continuation? For now start only when line looks like a decl
            if is_likely_member_function_start(scrub_preview) and not scrub_preview.strip().startswith(("if ", "for ", "while ", "switch ")):
                # Avoid matching nested local stuff — we're at depth 1
                # Avoid `std::atomic<bool> x{false};` brace-init
                if re.search(r"\w+\s*\(", scrub_preview) or re.search(r"MainWindow\s*\(", scrub_preview) or "explicit" in scrub_preview:
                    # Brace-init members: `Foo bar{1};` has { but often no (
                    if "(" in scrub_preview:
                        decl_start = i

        # Walk chars updating depth; if we open `{` while pending decl at depth 1 -> method body
        j = 0
        while j < len(line):
            ch = line[j]
            nxt = line[j + 1] if j + 1 < len(line) else ""
            if in_block:
                if ch == "*" and nxt == "/":
                    in_block = False
                    j += 2
                    continue
                j += 1
                continue
            if in_string:
                if ch == "\\" and j + 1 < len(line):
                    j += 2
                    continue
                if ch == in_string:
                    in_string = None
                j += 1
                continue
            if ch == "/" and nxt == "/":
                break
            if ch == "/" and nxt == "*":
                in_block = True
                j += 2
                continue
            if ch in ('"', "'"):
                in_string = ch
                j += 1
                continue
            if ch == "{":
                if abs_depth == 1 and decl_start is not None:
                    # This opens the function (or nested type). Distinguish nested type:
                    decl_text = "".join(lines[decl_start : i + 1])
                    head = decl_text
                    # If decl is struct/class/enum, skip — don't extract nested type
                    head_stripped = "".join(
                        strip_strings_and_comments(x, False, None)[0] for x in lines[decl_start : i + 1]
                    ).strip()
                    if re.match(r"^(struct|class|enum)\b", head_stripped):
                        decl_start = None
                        abs_depth += 1
                        j += 1
                        continue
                    # Brace-init members like `Foo bar{baz()};` have `{` before any
                    # completed parameter list — do not treat as a method body.
                    pre = "".join(lines[decl_start:i]) + line[:j]
                    paren = 0
                    brace_tmp = 0
                    saw_params = False
                    closed_params = False
                    in_s = None
                    for chx in pre:
                        if in_s:
                            if chx == in_s:
                                in_s = None
                            continue
                        if chx in ('"', "'"):
                            in_s = chx
                            continue
                        if chx == "{":
                            brace_tmp += 1
                        elif chx == "}":
                            brace_tmp = max(0, brace_tmp - 1)
                        elif chx == "(" and brace_tmp == 0:
                            paren += 1
                            saw_params = True
                        elif chx == ")" and brace_tmp == 0:
                            paren -= 1
                            if saw_params and paren == 0:
                                closed_params = True
                    if not closed_params:
                        # Either member brace-init (`Foo bar{...};`) or a default-arg
                        # brace (`= GuiRuntimeConfig{}`) while a param list is still open.
                        # Keep decl_start when still inside an open parameter list.
                        if paren > 0:
                            abs_depth += 1
                            j += 1
                            continue
                        decl_start = None
                        abs_depth += 1
                        j += 1
                        continue
                    # Function body starts here
                    end_l, end_c = find_matching_brace(lines, i, j)
                    methods.append((decl_start, i, j, end_l, end_c))
                    # Skip to end of method
                    decl_start = None
                    i = end_l
                    # set abs_depth: after closing brace of method we're back to 1
                    # Advance j past }; on that line by breaking to next i loop
                    # Manually consume rest of end line braces state
                    abs_depth = 1
                    # Move to character after closing brace
                    # Continue outer loop from end_l, scanning remainder of that line
                    rem = lines[end_l]
                    # Parse remainder after end_c for state
                    k = end_c + 1
                    while k < len(rem):
                        c2 = rem[k]
                        n2 = rem[k + 1] if k + 1 < len(rem) else ""
                        if in_block:
                            if c2 == "*" and n2 == "/":
                                in_block = False
                                k += 2
                                continue
                            k += 1
                            continue
                        if in_string:
                            if c2 == "\\" and k + 1 < len(rem):
                                k += 2
                                continue
                            if c2 == in_string:
                                in_string = None
                            k += 1
                            continue
                        if c2 == "/" and n2 == "/":
                            break
                        if c2 == "/" and n2 == "*":
                            in_block = True
                            k += 2
                            continue
                        if c2 in ('"', "'"):
                            in_string = c2
                            k += 1
                            continue
                        # ignore other braces at depth 1 (shouldn't have)
                        k += 1
                    break  # next i
                abs_depth += 1
            elif ch == "}":
                abs_depth -= 1
                if abs_depth < 1:
                    abs_depth = 1  # shouldn't happen before class end
            j += 1

        # If we thought we started a decl but hit `;` at depth 1 without `{`, cancel
        if decl_start is not None and abs_depth == 1:
            scrub, _, _ = strip_strings_and_comments(line, False, None)
            if ";" in scrub and "{" not in scrub:
                # Could be end of declaration without body — cancel
                # But multi-line signature may have no ; until later
                # Only cancel if the ; closes the declaration (no pending open paren)
                chunk = "".join(lines[decl_start : i + 1])
                sc, _, _ = strip_strings_and_comments(chunk, False, None) if False else (chunk, None, None)
                # crude paren balance
                if sc.count("(") <= sc.count(")") and ";" in scrub:
                    decl_start = None

        i += 1

    print(f"found {len(methods)} member functions to extract")
    if len(methods) < 40:
        for ds, bl, bc, el, ec in methods:
            chunk = "".join(lines[ds : bl + 1])[:80].replace("\n", " ")
            print(f"  {ds+1}-{el+1}: {chunk}")
        raise SystemExit("expected ~56 methods; aborting")

    # Build new header and cpp from methods (process from end to start for splicing)
    cpp_parts: list[str] = []
    new_lines = lines[:]  # copy

    for ds, bl, bc, el, ec in reversed(methods):
        decl_and_open = "".join(new_lines[ds : bl + 1])
        # body including braces
        if bl == el:
            body = new_lines[bl][bc : ec + 1]
            # May need newline
            body_text = body
            if not body_text.endswith("\n"):
                # take through ec on same line
                body_text = new_lines[bl][bc : ec + 1]
                # for replacement we'll handle
            body_full = body_text
        else:
            first = new_lines[bl][bc:]
            mid = "".join(new_lines[bl + 1 : el])
            last = new_lines[el][: ec + 1]
            body_full = first + mid + last
            if not body_full.endswith("\n"):
                # include trailing after } on last line? no
                pass

        # Declaration text without the `{...}` body — keep signature through char before `{`
        if bl == ds:
            sig_text = new_lines[ds][:bc]
        else:
            sig_text = "".join(new_lines[ds:bl]) + new_lines[bl][:bc]

        try:
            out_of_line = build_out_of_line(sig_text, body_full if body_full.endswith("\n") else body_full + "\n")
        except Exception as ex:
            preview = sig_text[:120].replace("\n", " ")
            raise RuntimeError(f"failed on lines {ds+1}-{el+1}: {preview}") from ex

        cpp_parts.append(out_of_line.rstrip() + "\n\n")

        # Replace in header with declaration ending in ;
        # Keep default args; drop ctor initializer list.
        try:
            decl = make_header_declaration(sig_text)
        except Exception as ex:
            preview = sig_text[:120].replace("\n", " ")
            raise RuntimeError(f"header decl failed on lines {ds+1}-{el+1}: {preview}") from ex
        # Preserve indent of first line
        indent = re.match(r"^(\s*)", new_lines[ds]).group(1)
        decl_lines = decl.splitlines()
        if decl_lines:
            # ensure first line keeps original indent if stripped
            if not decl_lines[0].startswith(indent):
                decl_lines[0] = indent + decl_lines[0].lstrip()
        replacement = "\n".join(decl_lines) + "\n"
        trailing = new_lines[el][ec + 1 :]
        if trailing.strip():
            replacement += trailing if trailing.endswith("\n") else trailing + "\n"
        new_lines[ds : el + 1] = [replacement]

    cpp_parts.reverse()

    # Move populateP25Table out of header too
    pop_start = None
    for i, line in enumerate(new_lines):
        if line.startswith("inline void populateP25Table"):
            pop_start = i
            break
    if pop_start is not None:
        # find opening brace
        brace_line = None
        brace_col = None
        for i in range(pop_start, pop_start + 10):
            if "{" in new_lines[i]:
                brace_line = i
                brace_col = new_lines[i].index("{")
                break
        assert brace_line is not None
        end_l, end_c = find_matching_brace(new_lines, brace_line, brace_col)
        sig = "".join(new_lines[pop_start:brace_line]) + new_lines[brace_line][:brace_col]
        sig = sig.replace("inline ", "", 1).rstrip()
        body = ""
        if brace_line == end_l:
            body = new_lines[brace_line][brace_col : end_c + 1]
        else:
            body = new_lines[brace_line][brace_col:] + "".join(new_lines[brace_line + 1 : end_l]) + new_lines[end_l][: end_c + 1]
        # Header keeps defaults; cpp definition strips them.
        hdr_decl = make_header_declaration(sig.replace("inline ", "", 1) if False else sig)
        # Ensure declaration text used for header still has original defaults from sig
        hdr_decl = make_header_declaration(sig)
        pop_impl = build_out_of_line(sig, body if body.endswith("\n") else body + "\n", class_name="")
        # build_out_of_line with empty class inserts "::name" — fix for free function
        name_and_ret, inner, quals, _init = split_params_and_rest(sig)
        parts = split_top_level_params(inner)
        cleaned = [strip_param_default(p) for p in parts]
        m = re.search(r"(~?\b\w+)\s*$", name_and_ret.rstrip())
        assert m
        prefix = name_and_ret[: m.start(1)].lstrip()
        free_sig = f"{prefix}{m.group(1)}(" + ", ".join(cleaned) + ")"
        if quals:
            free_sig += " " + quals
        pop_impl = free_sig + "\n" + (body if body.endswith("\n") else body + "\n")
        cpp_parts.insert(0, pop_impl.rstrip() + "\n\n")
        new_lines[pop_start : end_l + 1] = [hdr_decl + "\n"]

    # Write header
    HDR.write_text("".join(new_lines), encoding="utf-8", newline="\n")
    print(f"wrote {HDR.relative_to(ROOT)} ({sum(1 for _ in new_lines)} lines)")

    # Write cpp
    cpp_header = '''#include "MainWindow.h"

#include <QCloseEvent>
#include <QSizePolicy>

// AUTOMOC: Q_OBJECT lives in MainWindow.h (SpectrumWidget / TranscriptWindow pattern).
// Method bodies moved out-of-line (ISS-0004 Phase B) — mechanical, no behavior change.

'''
    CPP.write_text(cpp_header + "".join(cpp_parts), encoding="utf-8", newline="\n")
    # Qualify nested MainWindow types at namespace scope; strip illegal override on defs.
    text = CPP.read_text(encoding="utf-8")
    for name in (
        "P25VoiceDecodeWorkPurge",
        "P25VoiceWorkerQueueSnapshot",
        "P25VoiceDecodeResult",
        "P25VoiceDecodeJob",
    ):
        text = re.sub(rf"(?<!MainWindow::)\b{name}\b", f"MainWindow::{name}", text)
    text = re.sub(r"\)\s+override(\s*[\{\n])", r")\1", text)
    CPP.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {CPP.relative_to(ROOT)} ({text.count(chr(10))} lines)")
    print(f"extracted {len(methods)} methods + populateP25Table")


if __name__ == "__main__":
    main()
