from pathlib import Path
import struct
import subprocess
from tempfile import TemporaryDirectory


def main():
    directory = Path(__file__).resolve().parent
    variants = [
        ("matmul32", "matmul.comp", []),
        ("matmul64", "matmul.comp", ["-DDLM_DOUBLE=1"]),
        ("vector32", "vector.comp", []),
        ("vector64", "vector.comp", ["-DDLM_DOUBLE=1"]),
        ("ntt", "vector.comp", ["-DDLM_NTT=1"]),
    ]
    lines = ["/* SPIR-V generated from matmul.comp and vector.comp, target Vulkan 1.0. */"]
    with TemporaryDirectory() as temp:
        for name, shader, defines in variants:
            binary = Path(temp, name + ".spv")
            subprocess.run(
                ["glslangValidator", "-V", "-g0", "--target-env", "vulkan1.0", *defines, shader, "-o", binary],
                cwd=directory,
                check=True,
            )
            subprocess.run(["spirv-val", "--target-env", "vulkan1.0", binary], check=True)
            data = binary.read_bytes()
            words = struct.unpack(f"<{len(data) // 4}I", data)
            lines.append(f"static const uint32_t dm_spv_{name}[] = {{")
            for start in range(0, len(words), 8):
                group = words[start : start + 8]
                lines.append("    " + " ".join(f"0x{word:08x}u," for word in group))
            lines.append("};")
    (directory / "spv.inc").write_text("\n".join(lines) + "\n", encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
