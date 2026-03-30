#!/usr/bin/env python3
"""
SM83 (Game Boy CPU) disassembler.
Reads a .gb ROM file and outputs a .md file with the disassembly.

Usage:
    python disassemble.py <rom.gb> [output.md]

Output format matches the SeaBoy debugger disassembly window:
    ADDR  HH [HH [HH]]  MNEMONIC
"""

import sys
import struct
from pathlib import Path

# ---------------------------------------------------------------------------
# Opcode table - (format_string, length)
# Format specifiers: {n8} = 8-bit imm, {n16} = 16-bit imm, {e8} = signed 8-bit
# ---------------------------------------------------------------------------
OPS = [
    # 0x00-0x0F
    ("NOP", 1),          ("LD BC,${:04X}", 3),  ("LD (BC),A", 1),    ("INC BC", 1),
    ("INC B", 1),        ("DEC B", 1),           ("LD B,${:02X}", 2), ("RLCA", 1),
    ("LD (${:04X}),SP", 3), ("ADD HL,BC", 1),   ("LD A,(BC)", 1),    ("DEC BC", 1),
    ("INC C", 1),        ("DEC C", 1),           ("LD C,${:02X}", 2), ("RRCA", 1),
    # 0x10-0x1F
    ("STOP", 1),         ("LD DE,${:04X}", 3),  ("LD (DE),A", 1),    ("INC DE", 1),
    ("INC D", 1),        ("DEC D", 1),           ("LD D,${:02X}", 2), ("RLA", 1),
    ("JR {:+d}", 2),     ("ADD HL,DE", 1),       ("LD A,(DE)", 1),    ("DEC DE", 1),
    ("INC E", 1),        ("DEC E", 1),           ("LD E,${:02X}", 2), ("RRA", 1),
    # 0x20-0x2F
    ("JR NZ,{:+d}", 2),  ("LD HL,${:04X}", 3),  ("LD (HL+),A", 1),   ("INC HL", 1),
    ("INC H", 1),        ("DEC H", 1),           ("LD H,${:02X}", 2), ("DAA", 1),
    ("JR Z,{:+d}", 2),   ("ADD HL,HL", 1),       ("LD A,(HL+)", 1),   ("DEC HL", 1),
    ("INC L", 1),        ("DEC L", 1),           ("LD L,${:02X}", 2), ("CPL", 1),
    # 0x30-0x3F
    ("JR NC,{:+d}", 2),  ("LD SP,${:04X}", 3),  ("LD (HL-),A", 1),   ("INC SP", 1),
    ("INC (HL)", 1),     ("DEC (HL)", 1),        ("LD (HL),${:02X}", 2), ("SCF", 1),
    ("JR C,{:+d}", 2),   ("ADD HL,SP", 1),       ("LD A,(HL-)", 1),   ("DEC SP", 1),
    ("INC A", 1),        ("DEC A", 1),           ("LD A,${:02X}", 2), ("CCF", 1),
    # 0x40-0x7F  (LD r8,r8 / HALT)
    ("LD B,B", 1),  ("LD B,C", 1),  ("LD B,D", 1),  ("LD B,E", 1),
    ("LD B,H", 1),  ("LD B,L", 1),  ("LD B,(HL)", 1), ("LD B,A", 1),
    ("LD C,B", 1),  ("LD C,C", 1),  ("LD C,D", 1),  ("LD C,E", 1),
    ("LD C,H", 1),  ("LD C,L", 1),  ("LD C,(HL)", 1), ("LD C,A", 1),
    ("LD D,B", 1),  ("LD D,C", 1),  ("LD D,D", 1),  ("LD D,E", 1),
    ("LD D,H", 1),  ("LD D,L", 1),  ("LD D,(HL)", 1), ("LD D,A", 1),
    ("LD E,B", 1),  ("LD E,C", 1),  ("LD E,D", 1),  ("LD E,E", 1),
    ("LD E,H", 1),  ("LD E,L", 1),  ("LD E,(HL)", 1), ("LD E,A", 1),
    ("LD H,B", 1),  ("LD H,C", 1),  ("LD H,D", 1),  ("LD H,E", 1),
    ("LD H,H", 1),  ("LD H,L", 1),  ("LD H,(HL)", 1), ("LD H,A", 1),
    ("LD L,B", 1),  ("LD L,C", 1),  ("LD L,D", 1),  ("LD L,E", 1),
    ("LD L,H", 1),  ("LD L,L", 1),  ("LD L,(HL)", 1), ("LD L,A", 1),
    ("LD (HL),B", 1), ("LD (HL),C", 1), ("LD (HL),D", 1), ("LD (HL),E", 1),
    ("LD (HL),H", 1), ("LD (HL),L", 1), ("HALT", 1),       ("LD (HL),A", 1),
    ("LD A,B", 1),  ("LD A,C", 1),  ("LD A,D", 1),  ("LD A,E", 1),
    ("LD A,H", 1),  ("LD A,L", 1),  ("LD A,(HL)", 1), ("LD A,A", 1),
    # 0x80-0xBF  (ALU r8)
    ("ADD A,B", 1), ("ADD A,C", 1), ("ADD A,D", 1), ("ADD A,E", 1),
    ("ADD A,H", 1), ("ADD A,L", 1), ("ADD A,(HL)", 1), ("ADD A,A", 1),
    ("ADC A,B", 1), ("ADC A,C", 1), ("ADC A,D", 1), ("ADC A,E", 1),
    ("ADC A,H", 1), ("ADC A,L", 1), ("ADC A,(HL)", 1), ("ADC A,A", 1),
    ("SUB B", 1),   ("SUB C", 1),   ("SUB D", 1),   ("SUB E", 1),
    ("SUB H", 1),   ("SUB L", 1),   ("SUB (HL)", 1), ("SUB A", 1),
    ("SBC A,B", 1), ("SBC A,C", 1), ("SBC A,D", 1), ("SBC A,E", 1),
    ("SBC A,H", 1), ("SBC A,L", 1), ("SBC A,(HL)", 1), ("SBC A,A", 1),
    ("AND B", 1),   ("AND C", 1),   ("AND D", 1),   ("AND E", 1),
    ("AND H", 1),   ("AND L", 1),   ("AND (HL)", 1), ("AND A", 1),
    ("XOR B", 1),   ("XOR C", 1),   ("XOR D", 1),   ("XOR E", 1),
    ("XOR H", 1),   ("XOR L", 1),   ("XOR (HL)", 1), ("XOR A", 1),
    ("OR B", 1),    ("OR C", 1),    ("OR D", 1),    ("OR E", 1),
    ("OR H", 1),    ("OR L", 1),    ("OR (HL)", 1), ("OR A", 1),
    ("CP B", 1),    ("CP C", 1),    ("CP D", 1),    ("CP E", 1),
    ("CP H", 1),    ("CP L", 1),    ("CP (HL)", 1), ("CP A", 1),
    # 0xC0-0xCF
    ("RET NZ", 1),          ("POP BC", 1),            ("JP NZ,${:04X}", 3),  ("JP ${:04X}", 3),
    ("CALL NZ,${:04X}", 3), ("PUSH BC", 1),           ("ADD A,${:02X}", 2),  ("RST 00H", 1),
    ("RET Z", 1),           ("RET", 1),               ("JP Z,${:04X}", 3),   ("PREFIX CB", 2),
    ("CALL Z,${:04X}", 3),  ("CALL ${:04X}", 3),      ("ADC A,${:02X}", 2),  ("RST 08H", 1),
    # 0xD0-0xDF
    ("RET NC", 1),          ("POP DE", 1),            ("JP NC,${:04X}", 3),  ("???", 1),
    ("CALL NC,${:04X}", 3), ("PUSH DE", 1),           ("SUB ${:02X}", 2),    ("RST 10H", 1),
    ("RET C", 1),           ("RETI", 1),              ("JP C,${:04X}", 3),   ("???", 1),
    ("CALL C,${:04X}", 3),  ("???", 1),               ("SBC A,${:02X}", 2),  ("RST 18H", 1),
    # 0xE0-0xEF
    ("LDH ($FF{:02X}),A", 2), ("POP HL", 1),          ("LD ($FF00+C),A", 1), ("???", 1),
    ("???", 1),               ("PUSH HL", 1),          ("AND ${:02X}", 2),    ("RST 20H", 1),
    ("ADD SP,{:+d}", 2),      ("JP HL", 1),            ("LD (${:04X}),A", 3), ("???", 1),
    ("???", 1),               ("???", 1),              ("XOR ${:02X}", 2),    ("RST 28H", 1),
    # 0xF0-0xFF
    ("LDH A,($FF{:02X})", 2), ("POP AF", 1),          ("LD A,($FF00+C)", 1), ("DI", 1),
    ("???", 1),               ("PUSH AF", 1),          ("OR ${:02X}", 2),     ("RST 30H", 1),
    ("LD HL,SP{:+d}", 2),     ("LD SP,HL", 1),        ("LD A,(${:04X})", 3), ("EI", 1),
    ("???", 1),               ("???", 1),             ("CP ${:02X}", 2),     ("RST 38H", 1),
]

# Opcodes that use a signed 8-bit operand (JR, ADD SP, LD HL,SP+)
SIGNED_OPS = {0x18, 0x20, 0x28, 0x30, 0x38, 0xE8, 0xF8}

R8 = ["B", "C", "D", "E", "H", "L", "(HL)", "A"]
CB_BASE = ["RLC", "RRC", "RL", "RR", "SLA", "SRA", "SWAP", "SRL"]


def decode_cb(sub: int) -> str:
    reg = sub & 0x07
    if sub < 0x40:
        return f"{CB_BASE[sub >> 3]} {R8[reg]}"
    elif sub < 0x80:
        return f"BIT {(sub >> 3) & 7},{R8[reg]}"
    elif sub < 0xC0:
        return f"RES {(sub >> 3) & 7},{R8[reg]}"
    else:
        return f"SET {(sub >> 3) & 7},{R8[reg]}"


def disassemble_one(rom: bytes, offset: int) -> tuple[int, str, str]:
    """Returns (length, hex_bytes_str, mnemonic) for the instruction at offset."""
    if offset >= len(rom):
        return 1, "??", "???"

    op = rom[offset]

    if op == 0xCB:
        sub = rom[offset + 1] if offset + 1 < len(rom) else 0
        mnemonic = decode_cb(sub)
        raw = f"{op:02X} {sub:02X}   "
        return 2, raw, mnemonic

    fmt, length = OPS[op]

    if length == 1:
        raw = f"{op:02X}      "
        mnemonic = fmt
    elif length == 2:
        n8 = rom[offset + 1] if offset + 1 < len(rom) else 0
        raw = f"{op:02X} {n8:02X}   "
        if op in SIGNED_OPS:
            mnemonic = fmt.format(struct.unpack("b", bytes([n8]))[0])
        else:
            mnemonic = fmt.format(n8)
    else:  # length == 3
        lo = rom[offset + 1] if offset + 1 < len(rom) else 0
        hi = rom[offset + 2] if offset + 2 < len(rom) else 0
        n16 = lo | (hi << 8)
        raw = f"{op:02X} {lo:02X} {hi:02X}"
        mnemonic = fmt.format(n16)

    return length, raw, mnemonic


def parse_rom_header(rom: bytes) -> dict:
    """Extract basic header info from the ROM."""
    info = {}
    if len(rom) < 0x150:
        return info

    # Title: 0x134-0x143 (up to 16 bytes, null-padded)
    title_bytes = rom[0x134:0x144]
    info["title"] = title_bytes.split(b"\x00")[0].decode("ascii", errors="replace").strip()

    # Cartridge type
    cart_types = {
        0x00: "ROM ONLY",          0x01: "MBC1",             0x02: "MBC1+RAM",
        0x03: "MBC1+RAM+BATTERY",  0x05: "MBC2",             0x06: "MBC2+BATTERY",
        0x0F: "MBC3+TIMER+BATTERY",0x10: "MBC3+TIMER+RAM+BATTERY",
        0x11: "MBC3",              0x12: "MBC3+RAM",         0x13: "MBC3+RAM+BATTERY",
        0x19: "MBC5",              0x1A: "MBC5+RAM",         0x1B: "MBC5+RAM+BATTERY",
        0x1C: "MBC5+RUMBLE",       0x1D: "MBC5+RUMBLE+RAM",  0x1E: "MBC5+RUMBLE+RAM+BATTERY",
    }
    info["cart_type"] = cart_types.get(rom[0x147], f"Unknown (0x{rom[0x147]:02X})")

    # ROM size
    rom_sizes = {0: "32KB", 1: "64KB", 2: "128KB", 3: "256KB", 4: "512KB",
                 5: "1MB", 6: "2MB", 7: "4MB", 8: "8MB"}
    info["rom_size"] = rom_sizes.get(rom[0x148], f"Unknown (0x{rom[0x148]:02X})")

    # CGB flag
    cgb = rom[0x143]
    if cgb == 0x80:
        info["cgb"] = "CGB compatible"
    elif cgb == 0xC0:
        info["cgb"] = "CGB only"
    else:
        info["cgb"] = "DMG"

    # Entry point
    info["entry"] = f"0x{rom[0x100]:02X} {rom[0x101]:02X} {rom[0x102]:02X} {rom[0x103]:02X}"

    return info


def disassemble_rom(rom: bytes) -> list[tuple[int, str, str]]:
    """
    Linear sweep disassembly of the entire ROM.
    Returns list of (pc, hex_bytes, mnemonic).
    The ROM is mapped starting at address 0x0000.
    """
    results = []
    offset = 0
    while offset < len(rom):
        pc = offset  # ROM address == file offset for bank 0 / linear view
        length, raw, mnemonic = disassemble_one(rom, offset)
        results.append((pc, raw, mnemonic))
        offset += length
    return results


def write_md(path: Path, rom_path: Path, rom: bytes, lines: list[tuple[int, str, str]]) -> None:
    header = parse_rom_header(rom)
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"# Disassembly: {rom_path.name}\n\n")
        if header:
            f.write("## ROM Header\n\n")
            f.write(f"| Field | Value |\n|---|---|\n")
            if "title" in header:     f.write(f"| Title       | {header['title']} |\n")
            if "cgb" in header:       f.write(f"| CGB         | {header['cgb']} |\n")
            if "cart_type" in header: f.write(f"| Cart Type   | {header['cart_type']} |\n")
            if "rom_size" in header:  f.write(f"| ROM Size    | {header['rom_size']} |\n")
            if "entry" in header:     f.write(f"| Entry Point | {header['entry']} |\n")
            f.write("\n")

        f.write("## Disassembly\n\n")
        f.write("```\n")
        f.write(f"{'PC':<6}  {'Bytes':<8}  Instruction\n")
        f.write("-" * 40 + "\n")

        NOP_THRESHOLD = 16
        i = 0
        while i < len(lines):
            # Count consecutive NOPs starting at i
            run = 0
            while i + run < len(lines) and lines[i + run][2] == "NOP":
                run += 1

            if run > NOP_THRESHOLD:
                # Emit first NOP
                pc, raw, mnemonic = lines[i]
                f.write(f"{pc:04X}  {raw}  {mnemonic}\n")
                # Emit ellipsis showing the skipped range
                skipped = run - 2
                f.write(f"          ... ({skipped} NOPs omitted)\n")
                # Emit last NOP
                pc, raw, mnemonic = lines[i + run - 1]
                f.write(f"{pc:04X}  {raw}  {mnemonic}\n")
                i += run
            else:
                pc, raw, mnemonic = lines[i]
                f.write(f"{pc:04X}  {raw}  {mnemonic}\n")
                i += 1

        f.write("```\n")


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: python disassemble.py <rom.gb> [output.md]")
        sys.exit(1)

    rom_path = Path(sys.argv[1])
    if not rom_path.exists():
        print(f"Error: file not found: {rom_path}")
        sys.exit(1)

    out_path = Path(sys.argv[2]) if len(sys.argv) >= 3 else rom_path.with_suffix(".md")

    rom = rom_path.read_bytes()
    print(f"ROM size: {len(rom)} bytes ({len(rom) // 1024} KB)")

    lines = disassemble_rom(rom)
    write_md(out_path, rom_path, rom, lines)

    print(f"Disassembled {len(lines)} instructions -> {out_path}")


if __name__ == "__main__":
    main()
