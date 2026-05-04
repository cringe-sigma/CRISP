"""Platform descriptor loader (no external YAML dep)."""
from __future__ import annotations
import os
import re
from pathlib import Path

CFG = Path(__file__).resolve().parents[1] / "config" / "platform_imx8mm.yaml"


def _parse_simple_yaml(text: str) -> dict:
    """Pure-stdlib mini-YAML: scalars, inline lists [a, b], block lists
    (- item), nested dicts (1-level)."""
    out: dict = {}
    stack = [(0, out)]
    pending_list_key: str | None = None  # track which key gets block-list items
    pending_list_indent: int = -1
    for raw in text.splitlines():
        line_no_comment = raw.split("#", 1)[0].rstrip()
        if not line_no_comment.strip():
            continue
        indent = len(line_no_comment) - len(line_no_comment.lstrip(" "))
        stripped = line_no_comment.strip()

        # Block-list item: '- value'
        if stripped.startswith("- "):
            val = stripped[2:].strip()
            # Find the dict that owns the most recent scalar-less key at
            # an indent strictly less than this item's indent.
            if pending_list_key is not None and indent > pending_list_indent:
                parent = stack[-1][1] if not isinstance(
                    stack[-1][1].get(pending_list_key), list) else None
                # Locate parent dict containing the pending key
                for ind, d in reversed(stack):
                    if pending_list_key in d:
                        if not isinstance(d[pending_list_key], list):
                            d[pending_list_key] = []
                        d[pending_list_key].append(_coerce(val))
                        break
            continue

        while stack and indent < stack[-1][0]:
            stack.pop()
        cur = stack[-1][1]
        if ":" not in stripped:
            continue
        k, _, v = stripped.partition(":")
        k = k.strip()
        v = v.strip()
        if v == "":
            # Could be a nested dict or a block-list parent. Defer:
            cur[k] = {}
            stack.append((indent + 2, cur[k]))
            pending_list_key = k
            pending_list_indent = indent
            continue
        if v.startswith("[") and v.endswith("]"):
            inner = v[1:-1].strip()
            cur[k] = [_coerce(x.strip()) for x in inner.split(",")] if inner else []
        else:
            cur[k] = _coerce(v)
        pending_list_key = None
    return out


def _coerce(v: str):
    if re.fullmatch(r"-?\d+", v):
        return int(v)
    if re.fullmatch(r"-?\d*\.\d+", v):
        return float(v)
    if v in ("true", "True"):
        return True
    if v in ("false", "False"):
        return False
    return v.strip('"').strip("'")


def load(path: os.PathLike | str | None = None) -> dict:
    p = Path(path) if path else CFG
    return _parse_simple_yaml(p.read_text())


if __name__ == "__main__":
    import json
    print(json.dumps(load(), indent=2))
