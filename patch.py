import struct

with open("unpacked/lib/arm64-v8a/libvrc_fast_crypto.so", "rb") as f:
    data = bytearray(f.read())

RET_TRUE = bytes.fromhex("20008052C0035FD6")
RET_ZERO = bytes.fromhex("0000805200008052C0035FD6")

e_phoff     = struct.unpack_from("<Q", data, 32)[0]
e_phentsize = struct.unpack_from("<H", data, 54)[0]
e_phnum     = struct.unpack_from("<H", data, 56)[0]

def rva_to_off(rva):
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        if struct.unpack_from("<I", data, off)[0] != 1: continue
        p_offset = struct.unpack_from("<Q", data, off+8)[0]
        p_vaddr  = struct.unpack_from("<Q", data, off+16)[0]
        p_filesz = struct.unpack_from("<Q", data, off+32)[0]
        if p_vaddr <= rva < p_vaddr + p_filesz:
            return rva - p_vaddr + p_offset
    return None

for rva, patch, label in [
    (0x000002273c, RET_TRUE, "verify_message"),
    (0x0000022530, RET_TRUE, "sign_message"),
    (0x00000223b8, RET_ZERO, "generate_signing_key"),
]:
    off = rva_to_off(rva)
    if off:
        data[off:off+len(patch)] = patch
        print(f"PATCHED: {label} @ 0x{off:x}")

with open("unpacked/lib/arm64-v8a/libvrc_fast_crypto.so", "wb") as f:
    f.write(data)
print("Done.")
