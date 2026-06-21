#!/usr/bin/env python3
"""Extract function coverage data from QEMU serial dump and generate HTML report.

Usage:
    python3 tools/extract_gcda.py build/profiling/gcda.raw

Expects the kernel to emit frames: [4 bytes "FUNC"] [4 byte LE count] [9 bytes per func: 8 byte addr + 1 byte called].
The script correlates addresses with ELF symbols and generates an HTML coverage report.
"""
import struct
import sys
import os
import subprocess
import html


def main() -> None:
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <gcda.raw>")
        sys.exit(1)

    raw_path = sys.argv[1]
    if not os.path.exists(raw_path):
        print(f"Error: {raw_path} not found")
        sys.exit(1)

    data = open(raw_path, "rb").read()
    build_dir = os.path.realpath(os.path.join(os.path.dirname(raw_path), ".."))

    # Find FUNC marker
    idx = data.find(b"FUNC")
    if idx < 0:
        print("Error: no FUNC marker found in serial dump")
        sys.exit(1)

    idx += 4
    count = struct.unpack("<I", data[idx : idx + 4])[0]
    idx += 4

    funcs = []
    for i in range(count):
        if idx + 9 > len(data):
            break
        addr = struct.unpack("<Q", data[idx : idx + 8])[0]
        called = data[idx + 8]
        funcs.append((addr, called))
        idx += 9

    print(f"Extracted {len(funcs)} function(s) from serial dump")

    # Get symbol table from kernel ELF
    kernel_elf = os.path.join(build_dir, "kernel.elf")
    if not os.path.exists(kernel_elf):
        print(f"Error: {kernel_elf} not found")
        sys.exit(1)

    result = subprocess.run(
        ["x86_64-elf-nm", "-n", kernel_elf],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print(f"nm error: {result.stderr}")
        sys.exit(1)

    # Build address -> symbol mapping
    addr_to_sym = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                addr = int(parts[0], 16)
                sym_type = parts[1]
                name = " ".join(parts[2:])
                if sym_type in ("T", "t", "W", "w"):
                    addr_to_sym[addr] = name
            except ValueError:
                pass

    # Map called functions
    called_list = []
    uncalled_list = []
    for addr, called in funcs:
        name = addr_to_sym.get(addr, f"0x{addr:016x}")
        entry = (name, addr, called)
        if called:
            called_list.append(entry)
        else:
            uncalled_list.append(entry)

    total = len(funcs)
    called_count = len(called_list)
    uncalled_count = len(uncalled_list)
    coverage_pct = (called_count / total * 100) if total > 0 else 0

    # Generate HTML report
    report_dir = os.path.join(build_dir, "profiling", "report")
    os.makedirs(report_dir, exist_ok=True)

    def fmt_addr(a):
        return f"0x{a:016x}"

    rows = ""
    for name, addr, called in sorted(called_list + uncalled_list, key=lambda x: x[0]):
        color = "#4CAF50" if called else "#f44336"
        status = "Called" if called else "Not called"
        rows += f"""<tr style="background-color: {color}22">
            <td><code>{html.escape(name)}</code></td>
            <td><code>{fmt_addr(addr)}</code></td>
            <td>{status}</td>
        </tr>\n"""

    html_content = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Jarvis RTOS Kernel Coverage Report</title>
<style>
body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; margin: 20px; }}
h1 {{ color: #333; }}
.summary {{ margin: 20px 0; padding: 15px; background: #f5f5f5; border-radius: 8px; }}
.summary span {{ font-weight: bold; }}
.green {{ color: #4CAF50; }}
.red {{ color: #f44336; }}
table {{ border-collapse: collapse; width: 100%; }}
th, td {{ text-align: left; padding: 8px; border-bottom: 1px solid #ddd; }}
th {{ background-color: #333; color: white; }}
tr:hover {{ background-color: #f5f5f5 !important; }}
</style>
</head>
<body>
<h1>Jarvis RTOS Kernel Coverage Report</h1>
<div class="summary">
    <p>Total functions: <span>{total}</span></p>
    <p>Called: <span class="green">{called_count}</span> ({coverage_pct:.1f}%)</p>
    <p>Not called: <span class="red">{uncalled_count}</span> ({100-coverage_pct:.1f}%)</p>
</div>
<table>
<tr><th>Function</th><th>Address</th><th>Status</th></tr>
{rows}
</table>
</body>
</html>"""

    report_path = os.path.join(report_dir, "index.html")
    with open(report_path, "w") as f:
        f.write(html_content)

    print(f"Coverage report: {report_path}")
    print(f"  {called_count}/{total} functions called ({coverage_pct:.1f}%)")


if __name__ == "__main__":
    main()