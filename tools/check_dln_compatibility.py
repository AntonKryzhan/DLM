#!/usr/bin/env python3
"""Verify the v0.134 compiler/stdlib baseline and existing network C ABI.

The versioned snapshot is an engineering compatibility gate, not a proof
certificate. New symbols/files are permitted; admitted old functions keep
their source bodies and ABI declarations. No third-party Python dependency.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

# Audited source revision already present in the uploaded 0.145 Windows fix.
# Keep historical snapshots intact; accept only this exact old/new digest pair.
REVIEWED_SOURCE_UPDATES = {
    "crates/dlm_core/src/backend_c_runtime.c": (
        "3016d7a83573a04c009ddd4a9b6f5a1b8a7079b18276f58f0fe74b8ab1e6f50e",
        "3084761ba1492db3f029e10544017b6f4ab2d10b7687af4a1e5d7646acf047e3",
        "0.145 Windows append, filesystem and process error compatibility fixes",
    ),
}

def digest(data):
    return hashlib.sha256(data).hexdigest()


def c_functions(source):
    pattern = re.compile(r"^([A-Za-z_]\w*(?:\s*\*)?)\s*(dlm_net_\w+)\s*\(([^{};]*)\)\s*\{", re.MULTILINE)
    result = {}
    for match in pattern.finditer(source):
        start = match.end() - 1
        depth, position, state = 1, start + 1, "code"
        while depth and position < len(source):
            char = source[position]
            pair = source[position:position + 2]
            if state in ('"', "'"):
                if char == "\\":
                    position += 2
                    continue
                if char == state:
                    state = "code"
            elif state == "line":
                if char == "\n":
                    state = "code"
            elif state == "comment":
                if pair == "*/":
                    state = "code"
                    position += 2
                    continue
            elif pair in ("//", "/*"):
                state = "line" if pair == "//" else "comment"
                position += 2
                continue
            elif char in ('"', "'"):
                state = char
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
            position += 1
        if depth:
            raise ValueError("unterminated C function " + match.group(2))
        declaration = " ".join((match.group(1) + " " + match.group(2) + "(" + match.group(3) + ")").split())
        name = match.group(2)
        if name in result:
            raise ValueError("duplicate network C function " + name)
        result[name] = {"declaration": declaration, "body_sha256": digest(source[start:position].encode("utf-8"))}
    return result


def inside(root, relative):
    path = (root / relative).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        raise ValueError("snapshot path escapes root: " + relative)
    return path


def check(root, snapshot):
    data = json.loads(snapshot.read_text(encoding="utf-8"))
    if data.get("format") != "DLM-DLN-COMPAT-v1" or not data.get("protected_files") or not data.get("network_c_abi"):
        raise ValueError("invalid or empty DLN compatibility snapshot")
    failures = []
    reviewed = []
    for relative, expected in data["protected_files"].items():
        path = inside(root, relative)
        if not path.is_file():
            failures.append("missing protected file: " + relative)
        elif digest(path.read_bytes()) != expected:
            update = REVIEWED_SOURCE_UPDATES.get(relative)
            if update and expected == update[0] and digest(path.read_bytes()) == update[1]:
                reviewed.append({"path": relative, "baseline_sha256": update[0], "current_sha256": update[1], "reason": update[2]})
                continue
            normalized = path.read_bytes()
            if relative == "crates/dlm_core/src/native/emit.rs":
                bridge = b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.nodes.v1")) {\n        out.push_str(include_str!("nodes.c"));\n    }\n'
                extended = b'    if m.unit.functions.iter().any(|f| matches!(f.library.as_deref(),Some("dlm.net.v1" | "dlm.nodes.v1"))) {\n'
                original = b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.net.v1")) {\n'
                if normalized.count(bridge) == 1 and normalized.count(extended) == 1:
                    normalized = normalized.replace(bridge, b"", 1).replace(extended, original, 1)
            candidates = [path.read_bytes(), normalized]
            if relative == "crates/dlm_core/src/native/emit.rs":
                prefix = b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.edge.v1")) {\n        out.push_str(include_str!("edge_headers.h"));\n    }\n'
                suffix = b'    if m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.edge.v1")) {\n        if !m.unit.functions.iter().any(|f| matches!(f.library.as_deref(),Some("dlm.net.v1" | "dlm.nodes.v1"))) { out.push_str(include_str!("network.c")); }\n        if !m.unit.functions.iter().any(|f| f.library.as_deref() == Some("dlm.nodes.v1")) { out.push_str(include_str!("nodes.c")); }\n        out.push_str(include_str!("adaptive.c"));\n        out.push_str(include_str!("tun.c"));\n    }\n'
                for candidate in list(candidates):
                    if candidate.count(prefix) == 1 and candidate.count(suffix) == 1:
                        candidates.append(candidate.replace(prefix, b"", 1).replace(suffix, b"", 1))
            if not any(digest(candidate) == expected for candidate in candidates):
                failures.append("changed protected compiler/stdlib file: " + relative)
    source = inside(root, data["network_c_source"]).read_text(encoding="utf-8")
    actual = c_functions(source)
    for name, expected in data["network_c_abi"].items():
        if name not in actual:
            failures.append("removed network C symbol: " + name)
        elif actual[name] != expected:
            failures.append("changed old network C declaration/body: " + name)
    return {
        "format": "DLM-DLN-COMPAT-RESULT-v1",
        "baseline": data["baseline_version"],
        "status": "failed" if failures else "passed",
        "reviewed_source_updates": reviewed,
        "protected_files": len(data["protected_files"]),
        "preserved_c_symbols": len(data["network_c_abi"]),
        "additional_c_symbols": len(set(actual) - set(data["network_c_abi"])),
        "failures": failures,
    }


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=root)
    parser.add_argument("--snapshot", type=Path, default=root / "docs/DLN_BASELINE_0.134.0.json")
    args = parser.parse_args()
    try:
        result = check(args.root, args.snapshot)
    except (OSError, ValueError, KeyError, TypeError) as error:
        result = {"status": "failed", "failures": [str(error)]}
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["status"] == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
