"""Platform descriptor loader (no external YAML dep)."""
from __future__ import annotations
import os
import re
from pathlib import Path

CFG = Path(__file__).resolve().parents[1] / "config" / "platform_imx8mm.yaml"


def _parse_simple_yaml(text: str) -> dict:
    """Pure-stdlib mini-YAML: scalars, lists [a, b], nested dicts (1-level)."""
    out: dict = {}
    stack = [(0, out)]
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].rstrip()
        if not line.strip():
            continue
        indent = len(line) - len(line.lstrip(" "))
        line = line.strip()
        while stack and indent < stack[-1][0]:
            stack.pop()
        cur = stack[-1][1]
        if ":" not in line:
            continue
        k, _, v = line.partition(":")
        k = k.strip()
        v = v.strip()
        if v == "":
            cur[k] = {}
            stack.append((indent + 2, cur[k]))
            continue
        if v.startswith("[") and v.endswith("]"):
            inner = v[1:-1].strip()
            cur[k] = [_coerce(x.strip()) for x in inner.split(",")] if inner else []
        else:
            cur[k] = _coerce(v)
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
