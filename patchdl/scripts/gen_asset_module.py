#!/usr/bin/env python3
import argparse
import mimetypes
from pathlib import Path


def c_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


def bytes_as_c_array(path):
    data = Path(path).read_bytes()
    lines = []
    for offset in range(0, len(data), 16):
        chunk = data[offset:offset + 16]
        lines.append("  " + ", ".join(f"0x{byte:02x}" for byte in chunk))
    return "{\n" + ",\n".join(lines) + "\n}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--path", required=True)
    parser.add_argument("file")
    args = parser.parse_args()

    mime = mimetypes.guess_type(args.path)[0] or "application/octet-stream"
    symbol = (
        "patchdl_asset_"
        + "".join(ch if ch.isalnum() else "_" for ch in args.path)
    )

    print('#include "patchdl_assets.h"')
    print()
    print(f"static const unsigned char {symbol}[] = {bytes_as_c_array(args.file)};")
    print()
    print("__attribute__((constructor)) static void")
    print(f"register_{symbol}(void) {{")
    print(
        f'  patchdl_asset_register("/{c_string(args.path)}", '
        f"(const void*){symbol}, sizeof({symbol}), "
        f'"{c_string(mime)}");'
    )
    print("}")


if __name__ == "__main__":
    main()
