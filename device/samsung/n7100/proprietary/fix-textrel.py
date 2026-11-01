#!/usr/bin/env python3
"""Make KitKat-era ARM blobs loadable by today's linker, which refuses text relocations.

Old bionic crt objects kept absolute addresses in literal pools inside .text:

executable (crtbegin_dynamic): _start keeps main, the structors array (preinit/init/
fini/ctors) and __libc_init there, 5 R_ARM_RELATIVE and 1 R_ARM_ABS32 relocations.
Rewritten position-independently:

    mov  r0, sp
    mov  r1, #0
    add  r2, pc, #<main>         main (thumb bit set) relative to pc
    add  r3, pc, #12             structors: the old literal pool, now all zero; for a
                                 dynamic executable __libc_init only looks at fini_array
                                 (NULL: no destructors at exit), the linker runs
                                 DT_PREINIT_ARRAY/DT_INIT_ARRAY itself
    ldr  r4, [pc, #0x1c]         offset from pc to a word in the zero-filled end of the
    add  r4, pc, r4              last bss page...
    ldr  r4, [r4]                ...where the ABS32 relocation of __libc_init now points
    blx  r4

shared library (crtbegin_so): __on_dlclose loads &__dso_handle from the word after it
(1 R_ARM_RELATIVE), rewritten as

    ldr  r0, [pc, #4]            offset from pc to __dso_handle
    add  r0, pc, r0
    b    __cxa_finalize          unchanged

The RELATIVE relocations become R_ARM_NONE, DT_TEXTREL becomes DT_DEBUG, DF_TEXTREL is
cleared. Checks the exact original instruction sequences before touching anything.

usage: fix-textrel.py <elf>
"""
import struct
import sys

R_ARM_NONE, R_ARM_ABS32, R_ARM_RELATIVE = 0, 2, 23
DT_NULL, DT_DEBUG, DT_TEXTREL, DT_FLAGS = 0, 21, 22, 30
DF_TEXTREL = 0x4

START = [0xE1A0000D, 0xE3A01000, 0xE59F2020, 0xE28F300C, 0xE59F401C, 0xE12FFF34,
         0xE3A00000, 0xE12FFF10]
ON_DLCLOSE = [0xE28F0004, 0xE5900000]  # add r0, pc, #4; ldr r0, [r0]; (b __cxa_finalize)


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


class Elf:
    def __init__(self, b):
        self.b = b
        phoff, shoff = u32(b, 0x1C), u32(b, 0x20)
        phnum = struct.unpack_from("<H", b, 0x2C)[0]
        shnum = struct.unpack_from("<H", b, 0x30)[0]
        self.entry = u32(b, 0x18)
        self.loads = []
        self.dyn = None
        for i in range(phnum):
            p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = struct.unpack_from(
                    "<8I", b, phoff + 32 * i)
            if p_type == 1:
                self.loads.append((p_offset, p_vaddr, p_filesz, p_memsz, p_flags))
            if p_type == 2:
                self.dyn = (p_offset, p_filesz)
        self.text = next(l for l in self.loads if l[4] & 1)  # PF_X
        # relocations into the executable segment
        self.relocs = []
        for i in range(shnum):
            sh_type, sh_offset, sh_size = (u32(b, shoff + 40 * i + 4), u32(b, shoff + 40 * i + 16),
                                           u32(b, shoff + 40 * i + 20))
            if sh_type == 9:  # SHT_REL
                for r in range(sh_offset, sh_offset + sh_size, 8):
                    r_offset, r_info = struct.unpack_from("<II", b, r)
                    if r_info & 0xFF != R_ARM_NONE and \
                            self.text[1] <= r_offset < self.text[1] + self.text[3]:
                        self.relocs.append((r, r_offset, r_info))

    def off(self, vaddr):
        for o, v, fs, _, _ in self.loads:
            if v <= vaddr < v + fs:
                return vaddr - v + o
        raise ValueError(hex(vaddr))

    def word(self, vaddr):
        return u32(self.b, self.off(vaddr))

    def put(self, vaddr, value):
        struct.pack_into("<I", self.b, self.off(vaddr), value & 0xFFFFFFFF)


def fix_start(e):
    """crtbegin_dynamic's _start; returns a description or None if it isn't there"""
    if [e.word(e.entry + 4 * i) for i in range(8)] != START:
        return None
    pool = e.entry + 0x20  # 4 structors, main, __libc_init
    kinds = sorted((ro - pool, ri & 0xFF) for _, ro, ri in e.relocs)
    if kinds != [(0, 23), (4, 23), (8, 23), (12, 23), (16, 23), (20, 2)]:
        sys.exit("unexpected text relocations: %s" % kinds)
    main_addr = e.word(pool + 0x10)
    # the end of the last writable segment's last page is mapped, zero and unused
    rw = max((l for l in e.loads if l[4] & 2 and not l[4] & 1), key=lambda l: l[1] + l[3])
    slot = (rw[1] + rw[3] + 15) & ~15
    if (slot & 0xFFF) > 0xFF0 or slot >= ((rw[1] + rw[3] + 0xFFF) & ~0xFFF):
        sys.exit("no room after bss")
    for r, r_offset, r_info in e.relocs:
        if r_info & 0xFF == R_ARM_RELATIVE:
            struct.pack_into("<II", e.b, r, 0, R_ARM_NONE)
        else:
            struct.pack_into("<II", e.b, r, slot, r_info)  # __libc_init -> the bss slot
    main_rel = main_addr - (e.entry + 0x8 + 8)
    if not 0 <= main_rel < 256:
        sys.exit("main too far for add r2, pc, #imm8")
    code = [0xE1A0000D, 0xE3A01000, 0xE28F2000 | main_rel, 0xE28F300C, 0xE59F401C,
            0xE08F4004, 0xE5944000, 0xE12FFF34]
    for i, w in enumerate(code):
        e.put(e.entry + 4 * i, w)
    for i in range(5):
        e.put(pool + 4 * i, 0)
    e.put(pool + 20, slot - (e.entry + 0x14 + 8))  # pc as read by "add r4, pc, r4"
    return "_start rewritten, main %#x, __libc_init via %#x" % (main_addr, slot)


def fix_on_dlclose(e):
    """crtbegin_so's __on_dlclose; returns a description or None if it isn't there"""
    if len(e.relocs) != 1:
        return None
    r, lit, r_info = e.relocs[0]
    fn = lit - 12
    if r_info & 0xFF != R_ARM_RELATIVE or [e.word(fn), e.word(fn + 4)] != ON_DLCLOSE or \
            e.word(fn + 8) >> 24 != 0xEA:
        return None
    dso_handle = e.word(lit)
    struct.pack_into("<II", e.b, r, 0, R_ARM_NONE)
    e.put(fn, 0xE59F0004)       # ldr r0, [pc, #4]
    e.put(fn + 4, 0xE08F0000)   # add r0, pc, r0
    e.put(lit, dso_handle - (fn + 4 + 8))
    return "__on_dlclose rewritten, __dso_handle %#x" % dso_handle


def main():
    path = sys.argv[1]
    e = Elf(bytearray(open(path, "rb").read()))
    if not e.relocs:
        sys.exit("%s: no text relocations" % path)
    what = fix_start(e) or fix_on_dlclose(e)
    if not what:
        sys.exit("%s: unknown text relocations at %s" % (path, [hex(ro) for _, ro, _ in e.relocs]))
    off, size = e.dyn
    for d in range(off, off + size, 8):
        tag, val = struct.unpack_from("<iI", e.b, d)
        if tag == DT_NULL:
            break
        if tag == DT_TEXTREL:
            struct.pack_into("<iI", e.b, d, DT_DEBUG, 0)
        if tag == DT_FLAGS:
            struct.pack_into("<iI", e.b, d, DT_FLAGS, val & ~DF_TEXTREL)
    open(path, "wb").write(e.b)
    print("%s: %s" % (path, what))


if __name__ == "__main__":
    main()
