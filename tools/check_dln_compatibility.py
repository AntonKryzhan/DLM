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

# Keep historical snapshots intact; admit only reviewed Windows revisions.
REVIEWED_SOURCE_UPDATES = {
    "crates/dlm_core/src/backend_c_runtime.c": (
        "3016d7a83573a04c009ddd4a9b6f5a1b8a7079b18276f58f0fe74b8ab1e6f50e",
        "e905d6e42c9a1a4fefdfd15202ed4fc3d579a0310386106981b9f5421a3b1ba5",
        "Windows append position/resize ownership, filesystem/process and TCP error compatibility fixes",
    ),
    "crates/dlm_core/src/native/network.c": (
        "39834f4c0ff5a87dfba895c96595ffcfd503991deea91b5caa096b0ca897c129",
        "50655336e7c20d8adae261a0f92569589392e9fcf4db3c9c50c3e9611b188fb8",
        "Windows Hogweed 7/6 dynamic loader compatibility; existing network C ABI unchanged",
    ),
    "crates/dlm_core/src/os.rs": (
        "95a0083920938dcca4dd0411133915b43405efbe71763323cf961c5df601f9e4",
        "6b80f90a38259a6e8f71eae2ccfc8a006f4dc2e5b5b830d45c28ae0d9102d2ff",
        "Windows interpreter NotFound and ConnectionRefused error normalization",
    ),
    "crates/dlm_core/src/runtime/ordinary_surface.rs": (
        "f5e1741e2070194d7c3d3b7635d410cf3cae3bbcc37f7fae10c59f744f4a5bad",
        "5741421408c70832a99908040ac3f7636549ebf88041f139a4890fcfd54d03b7",
        "Windows interpreter append resize ownership and filesystem error normalization",
    ),
}

# Admit only the reviewed cumulative v0.150.0 native extensions, without
# rewriting any historical DLN baseline or weakening the old network C ABI gate.
REVIEWED_NATIVE_S2_UPDATES = {
    "crates/dlm_core/src/native/check.rs": (
        ("d524a11875ddc1d881b6316073a28926d75772ec21ffdc6cd151e02c8a2dc99c",),
        "2c12f89e4441d275ae4c64c4be1d305dc18fe5e0144ec37afa04caca230b8a4e",
        "v0.150.0 checked parallel, async, ownership, ABI, SQL and HTTP intrinsics",
    ),
    "crates/dlm_core/src/native/emit.rs": (
        (
            "ff5bf192c9ffdf52a3cd1a8282ff73c5c7ca085ec6426a31d069a8223906aed5",
            "4ab50592188ff2bdd88fda1f0418a08a48713a21c13a40359c2e2d2aba8b323e",
        ),
        "25dfc80490556ec152c8b48e8d1465e121d7ce572d92dc6c5c33e3f456d4d008",
        "v0.150.0 parallel, async, SQL and HTTP native emission",
    ),
    "crates/dlm_core/src/native/mod.rs": (
        ("2e9c9ad203bd2ef252fd9d0f076e1bdea8a4df548826a84422fd2098dc46d9ed",),
        "bba097c9064260010720f70005fb7e42a663e8fda7116ee8e4429ab0be26f388",
        "v0.150.0 native concurrency and database type declarations",
    ),
    "crates/dlm_core/src/native/runtime.c": (
        ("1b8621c82b50a824cb5fab5bfac89689a5fe7ce9e0339a8c3eb1b52946c43265",),
        "4bed4ed58c12c4540d35de239cbf95f2883d1b10d5a53cfa5d1bad9468e3bf4a",
        "v0.150.0 locale-independent decimal parsing for JSON",
    ),
    "crates/dlm_core/src/native/syntax.rs": (
        ("5757b4aa201ce0754f8202e22d818ddc18bad905897bd40078ccfd5f58496e16",),
        "b456964e5ef2f0c51e856e99f43117ed3cc38752f3bb494a4e53e6d3e0d03900",
        "v0.150.0 concurrency and DbPool native type syntax",
    ),
    "crates/dlm_core/src/native/vm.rs": (
        ("bf1ad257925ec86e6cf590cd4417f14447adbe6d0452d49d161cbc7de4f15250",),
        "5d567045aa413d6905ec7e4bfbd95734e7fef90ebcfc5a8bb9167cd6de22ece9",
        "v0.150.0 parallel, async and JSON native VM integration",
    ),
    "crates/dlm_core/src/runtime.rs": (
        ("ce2b1acb4ca6224db4f7681f83753acc58b0157ff2715576101ce7f4fb391f47",),
        "a3886ab10002f2fafa9e14edf614950abdd8a84a22067fe812c1d0cd32b2f911",
        "v0.150.0 native concurrency runtime integration",
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
            current_digest = digest(path.read_bytes())
            update = REVIEWED_SOURCE_UPDATES.get(relative)
            if update and expected == update[0] and current_digest == update[1]:
                reviewed.append({"path": relative, "baseline_sha256": update[0], "current_sha256": update[1], "reason": update[2]})
                continue
            update = REVIEWED_NATIVE_S2_UPDATES.get(relative)
            if update and expected in update[0] and current_digest == update[1]:
                reviewed.append({"path": relative, "baseline_sha256": expected, "current_sha256": update[1], "reason": update[2]})
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
