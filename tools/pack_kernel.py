#!/usr/bin/env python3
"""
pack_kernel.py - Create a self-decompressing kernel ELF.

Usage: pack_kernel.py decompressor.elf kernel.bin.gz output.elf

1. Reads the decompressor ELF (multiboot2 compatible)
2. Reads the compressed kernel binary
3. Patches the compressed_data_len symbol in the decompressor
4. Adds a PT_LOAD segment at 0x4001000 for the compressed payload
5. Writes the final packed kernel ELF
"""
import struct, sys, os

def align_up(v, a):
    return (v + a - 1) & ~(a - 1)

def main():
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} decompressor.elf kernel.bin.gz output.elf")
        sys.exit(1)

    decomp_path = sys.argv[1]
    payload_path = sys.argv[2]
    out_path = sys.argv[3]

    with open(decomp_path, 'rb') as f:
        elf = bytearray(f.read())

    with open(payload_path, 'rb') as f:
        payload = f.read()

    # Parse ELF64 header
    if elf[0:4] != b'\x7fELF' or elf[4] != 2:
        print("Not a 64-bit ELF")
        return 1

    e_phoff = struct.unpack_from('<Q', elf, 0x20)[0]
    e_phentsize = struct.unpack_from('<H', elf, 0x36)[0]
    e_phnum = struct.unpack_from('<H', elf, 0x38)[0]
    e_shoff = struct.unpack_from('<Q', elf, 0x28)[0]
    e_shentsize = struct.unpack_from('<H', elf, 0x3A)[0]
    e_shnum = struct.unpack_from('<H', elf, 0x3C)[0]
    e_shstrndx = struct.unpack_from('<H', elf, 0x3E)[0]

    # Find compressed_data_len symbol in symtab
    symtab_off = None
    symtab_entsize = 0
    symtab_count = 0
    strtab_off = None
    strtab_size = 0

    if e_shoff and e_shnum:
        for i in range(e_shnum):
            sh_off = e_shoff + i * e_shentsize
            sh_name = struct.unpack_from('<I', elf, sh_off)[0]
            sh_type = struct.unpack_from('<I', elf, sh_off + 4)[0]
            sh_flags = struct.unpack_from('<Q', elf, sh_off + 8)[0]
            sh_addr = struct.unpack_from('<Q', elf, sh_off + 0x10)[0]
            sh_offset = struct.unpack_from('<Q', elf, sh_off + 0x18)[0]
            sh_size = struct.unpack_from('<Q', elf, sh_off + 0x20)[0]
            sh_entsize = struct.unpack_from('<Q', elf, sh_off + 0x38)[0]

            if sh_type == 2:  # SHT_SYMTAB
                symtab_off = sh_offset
                symtab_entsize = sh_entsize
                symtab_count = sh_size // sh_entsize if sh_entsize else 0
            if sh_type == 3:  # SHT_STRTAB
                if e_shstrndx != i:  # Not the section name string table
                    strtab_off = sh_offset
                    strtab_size = sh_size

    # Find compressed_data_len symbol in symtab and patch it
    patched = False
    p_align = 0x1000
    payload_load_vaddr = 0x4001000
    compressed_vma = None

    # First, find the symbol value for compressed_data_len from symtab
    if symtab_off is not None:
        for j in range(symtab_count):
            e_off = symtab_off + j * symtab_entsize
            st_name = struct.unpack_from('<I', elf, e_off)[0]
            st_value = struct.unpack_from('<Q', elf, e_off + 8)[0]
            st_info = elf[e_off + 4]
            st_other = elf[e_off + 5]
            st_shndx = struct.unpack_from('<H', elf, e_off + 6)[0]

            if st_name and strtab_off is not None:
                name_end = elf.find(b'\0', strtab_off + st_name)
                if name_end > strtab_off:
                    name = elf[strtab_off + st_name:name_end].decode('ascii', errors='replace')
                    if name == 'compressed_data_len':
                        compressed_vma = st_value
                        break

    # Now find which PT_LOAD segment contains this VMA and patch
    if compressed_vma is not None:
        for i in range(e_phnum):
            ph_off = e_phoff + i * e_phentsize
            p_type = struct.unpack_from('<I', elf, ph_off)[0]
            p_offset = struct.unpack_from('<Q', elf, ph_off + 8)[0]
            p_vaddr = struct.unpack_from('<Q', elf, ph_off + 0x10)[0]
            p_filesz = struct.unpack_from('<Q', elf, ph_off + 0x20)[0]

            if p_type == 1 and p_vaddr <= compressed_vma < p_vaddr + p_filesz:
                file_offset = p_offset + (compressed_vma - p_vaddr)
                payload_len = len(payload)
                struct.pack_into('<Q', elf, file_offset, payload_len)
                print(f"  Patched compressed_data_len = {payload_len} at VMA 0x{compressed_vma:x} file 0x{file_offset:x}")
                patched = True
                break

    if not patched:
        if compressed_vma is not None:
            print(f"  Warning: found compressed_data_len at VMA 0x{compressed_vma:x} but couldn't find segment")
        else:
            print("  Warning: compressed_data_len symbol not found in symbol table")

    # Add a new PT_LOAD program header for the compressed payload
    # The ELF has e_phentsize-sized headers; extend it
    new_phnum = e_phnum + 1
    # Align file offset for the new segment data
    new_payload_offset = align_up(len(elf), p_align)
    # Add the payload to the ELF data
    payload_padded = payload
    # Align payload size
    payload_filesz = len(payload)
    payload_memsz = payload_filesz
    payload_flags = 2  # PT_LOAD, PF_R

    # Create new program header at the end of existing program headers
    new_ph_offset = e_phoff + e_phnum * e_phentsize
    # Expand ELF to hold the new program header
    while len(elf) < new_ph_offset + e_phentsize:
        elf.append(0)

    # Write the new program header
    # p_type (4 bytes)
    struct.pack_into('<I', elf, new_ph_offset, 1)  # PT_LOAD
    # p_flags (4 bytes) - actually at offset 4 for 64-bit ELF PH
    struct.pack_into('<I', elf, new_ph_offset + 4, 4)  # PF_R
    # p_offset (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 8, new_payload_offset)
    # p_vaddr (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 0x10, payload_load_vaddr)
    # p_paddr (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 0x18, payload_load_vaddr)
    # p_filesz (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 0x20, payload_filesz)
    # p_memsz (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 0x28, payload_memsz)
    # p_align (8 bytes)
    struct.pack_into('<Q', elf, new_ph_offset + 0x30, p_align)

    # Update e_phnum in ELF header
    struct.pack_into('<H', elf, 0x38, new_phnum)

    # Write payload data
    if new_payload_offset + payload_filesz > len(elf):
        elf.extend(b'\x00' * (new_payload_offset + payload_filesz - len(elf)))
    elf[new_payload_offset:new_payload_offset + payload_filesz] = payload

    # Write output
    with open(out_path, 'wb') as f:
        f.write(elf)

    orig_size = os.path.getsize(decomp_path)
    final_size = len(elf)
    print(f"  Decompressor: {orig_size} bytes")
    print(f"  Payload: {len(payload)} bytes compressed")
    print(f"  Final ELF: {final_size} bytes")
    print(f"  Written to {out_path}")

if __name__ == '__main__':
    sys.exit(main())
