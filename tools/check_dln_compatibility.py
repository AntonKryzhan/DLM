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

# Keep historical snapshots intact; admit only reviewed source revisions.
REVIEWED_SOURCE_UPDATES = {
    "crates/dlm_core/src/backend_c_runtime.c": (
        "3016d7a83573a04c009ddd4a9b6f5a1b8a7079b18276f58f0fe74b8ab1e6f50e",
        "5dd11a83d68fd38bda34dde0f12a86c13f47b2f82dc4be8f8f6b44c0ca4537dd",
        "Windows compatibility fixes and v0.151.0 math compute runtime integration",
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
        "cf77e2008fc20c35df6b39110fd50873e34891889c2bf31a6a5cea7816e8228f",
        "Windows interpreter compatibility and v0.151.0 math dispatch integration",
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
        "efe9b6eff6f9548735fca761c11a6cd97359acab3e8dda2ad66668a8cb5bef5c",
        "v0.150.0 native concurrency and v0.151.0 math runtime integration",
    ),
}

# The final v0.151.0 math patchset changes these protected compiler sources.
# Keep every historical baseline digest and the network C ABI snapshots intact.
REVIEWED_MATH_UPDATES = {
    "crates/dlm_core/src/ast.rs": (
        "652e50a82efe9a9cc2be6806082c1bb2de32bbd8023cbabb8b65f2ef099a1f3c",
        "584a9009150642d9e925d30168d55d9876359872e58955c91df8d3cc38a0988f",
    ),
    "crates/dlm_core/src/audit.rs": (
        "9f50f66052b0d444800631bc012b02f83771eb3f3444ff4c95bfdbe5ffa3808f",
        "e2456d7e6a05e1e9cbab1f6cb2c59846e4e0ebae50d90bdcf5930468e7e071a2",
    ),
    "crates/dlm_core/src/backend_c.rs": (
        "de59203e7209d5904b41680ca121dccad4b45260ea8701c544674f09ed608e45",
        "9ba3e0791def6333d757aa054b500e0f368f15f1af02cc08b1cf65508155660c",
    ),
    "crates/dlm_core/src/certificate.rs": (
        "af1ab9fb74f92d5ef5f8b7970a7e993173dd580686833402fe5bad30fcc96d58",
        "bf9ef870169a0f21f41ab1836f0c04c5b1b865b681f99c549a1f8a15f535fbbe",
    ),
    "crates/dlm_core/src/certificate_audit.rs": (
        "263f4bbe9eada4f4cf382bf80ba84ea04bbf25a91b813efe919dc7b62db243bb",
        "dce9848a3aebd71ba21ecabc3a0180c8d91cc63c139d9873db15db339c6081d9",
    ),
    "crates/dlm_core/src/checker.rs": (
        "bcc417b252db57fbbf3f8d0b2a775132f31ce3a1ba6c8d05f7e83a74dbd85d3b",
        "72723700c38f487fef1f3674daa0592678ed4253a8dbc05a2082203c18808280",
    ),
    "crates/dlm_core/src/checker/modules.rs": (
        "9cd017ff6e6c79add47df950efc53798974818ab4164da6d1a5657a1c5029420",
        "c9196c49d0d3d81e50f474203d326e58bcaa0e9e2c1982416ac44a51a9d03806",
    ),
    "crates/dlm_core/src/checker/ordinary_surface.rs": (
        "88ace1a86883f7652f50424812e8ef2c23e2efc99198cdb560e025ebc2e21c83",
        "5f30e481fb250fb839e7db865db9a21064309942579ea175d1dde7b6f60bf2ec",
    ),
    "crates/dlm_core/src/checker/statements.rs": (
        "dae7d5f3b51d6ac2966ea38760e085bb2cfb9487ba565315d141332c6ff24d31",
        "3573b367b3f857644d9a520aed972c426a18afc791209de09671f836d6655325",
    ),
    "crates/dlm_core/src/compile.rs": (
        "804a48db6971ff861e426a0cf930d85a412d1f875e3b1f0737efec0af4d7fe2e",
        "1aa3d0bb062005e430bcdfbf8dafae033f518dd7975035a3a19577bec2c7f8fc",
    ),
    "crates/dlm_core/src/float.rs": (
        "089ff48d17c448031fe5a31f27c801ac55faebed747d586bbdde1f34b209bbe3",
        "91b815e3c186a493702cbe792c6bf767234b8c932d472fa88b8c4d31cd065381",
    ),
    "crates/dlm_core/src/generics.rs": (
        "67858020c6a8f2fd3f77119a966958f91e25a6cda724fd1f14b8fce73ff5fbe0",
        "aec24490f779eb487528e71612353f2d30b6790a318c64145fae9a65d0c3e94c",
    ),
    "crates/dlm_core/src/ir_verify/builtins.rs": (
        "3e3babd0fe59adf4770fc94e0632ed5ee78c58ea754377f344dfbe481dca5f9d",
        "da44ddb08dc0fe8256e51e3583645fbd222337e6e726ca06775b447eacbc0238",
    ),
    "crates/dlm_core/src/lib.rs": (
        "4e75a842b05d350d88173451c2ce29678fc3eb327d39184557b818349add6c84",
        "97891f65299a6834630fd3079ebbbceeb8141c2af79af7c079810eb1f4279dbd",
    ),
    "crates/dlm_core/src/linalg.rs": (
        "0b759ce0cfa1534836f4ac73747ed2366e11b522dcb341d8ab9098c3f944aac2",
        "5c97ce993c16152b4e651d6e941aaeb9f64352bf3de2f3eedd6f71cc4bbac247",
    ),
    "crates/dlm_core/src/math/mod.rs": (
        "47147433a38e294c0537806cf83dc32dadc9ccae7948ddd17d33bdf04ae81014",
        "fa2644b83479cff1ee567b7fe94176a4488170bf90eb6369522fe5b12a45c6de",
    ),
    "crates/dlm_core/src/native/build.rs": (
        "96d491892c36bd52f0127432b69ab6df0578e9604fa3f797474364bf958262bd",
        "b0930362751e4efdfb40e08ddec3532f466414795d6d232222a351052b0217ad",
    ),
    "crates/dlm_core/src/parser.rs": (
        "b1649d39bddd72912d24ad63ef63cb2752218a0ff90360150156b0481bd0db8c",
        "a6b0f1c921bc4df0e2b0029efcc30e9f69beb6cc674e001481659f1402b419c7",
    ),
    "crates/dlm_core/src/resolve.rs": (
        "d9ddb7129d9feb0fa78b379fe63910ba903fec816b04205e982e7e17e8bb2544",
        "58d0fa757da702735fab71aed2a9c8903dbc215bb62281ddc3e03d667faccc3c",
    ),
    "crates/dlm_core/src/spectral.rs": (
        "7e0b9ca688f0059dd1176a3926ae118ca061b6e1ecda7c1c591b147ecafff1ce",
        "7aa45f6122944769671e760df29dedfc505afedaefa5aa750a485c6ae55fc374",
    ),
    "crates/dlm_core/src/typed_core.rs": (
        "49cc3ed3734fecd7f413ba2de3985d604dff4b2e3cb6045fed7928f14c33822f",
        "66c0f908127a31410f276b0003986eff4bb9bf19382d181c7b76d2733e31bb3d",
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
            update = REVIEWED_MATH_UPDATES.get(relative)
            if update and expected == update[0] and current_digest == update[1]:
                reviewed.append({"path": relative, "baseline_sha256": expected, "current_sha256": update[1],
                                 "reason": "v0.151.0 Riemann theorem binding, math dispatch and math compute integration"})
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
