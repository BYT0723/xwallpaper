#!/usr/bin/env python3
"""Generate tiny synthetic scene fixtures (<50KB) for unit tests.

Outputs in tests/data/scene/:
  mini.pkg       - PKGV container with 2 files (hello.txt, data.bin)
  mini.tex       - TEXV + embedded PNG (copied from tests/data/red.png if present)
  shader_sample.txt - minimal WE-shader sample for translate tests
"""
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def write_pkg(path):
    files = {
        "hello.txt": b"hello scene",
        "data.bin": bytes(range(16)),
        "sub/dir.bin": b"\xde\xad\xbe\xef",
    }
    names = list(files.keys())
    blobs = [files[n] for n in names]
    n = len(names)
    # real PKGV layout: u32 ignored + magic + version + u32 count,
    # entries as u32 namelen + name + u32 offset + u32 size,
    # offsets relative to the end of the file table.
    header = struct.pack("<I", 8) + b"PKGV" + b"0008" + struct.pack("<I", n)
    entries = b""
    off = 0
    for nm, blob in zip(names, blobs):
        nb = nm.encode()
        entries += struct.pack("<I", len(nb)) + nb
        entries += struct.pack("<II", off, len(blob))
        off += len(blob)
    with open(path, "wb") as f:
        f.write(header + entries + b"".join(blobs))


def write_tex(path):
    png = None
    red = os.path.join(HERE, "..", "red.png")
    if os.path.exists(red):
        with open(red, "rb") as f:
            png = f.read()
    else:
        # minimal 1x1 PNG (opaque red) fallback
        png = bytes.fromhex(
            "89504e470d0a1a0a0000000d4948445200000001000000010802000000907753de"
            "0000000c4944415478daf600010000020001e221bc330000000049454e44ae426082"
        )
    with open(path, "wb") as f:
        f.write(b"TEXV")
        f.write(struct.pack("<II", 1, 1))
        f.write(png)


def write_shader(path):
    with open(path, "w") as f:
        f.write("// WE shader sample\nvoid main() { gl_FragColor = vec4(1.0); }\n")


def main():
    os.makedirs(HERE, exist_ok=True)
    write_pkg(os.path.join(HERE, "mini.pkg"))
    write_tex(os.path.join(HERE, "mini.tex"))
    write_shader(os.path.join(HERE, "shader_sample.txt"))
    for fn in ("mini.pkg", "mini.tex", "shader_sample.txt"):
        sz = os.path.getsize(os.path.join(HERE, fn))
        print(f"{fn}: {sz} bytes")
        assert sz < 50 * 1024, f"{fn} too big"


if __name__ == "__main__":
    sys.exit(main())
