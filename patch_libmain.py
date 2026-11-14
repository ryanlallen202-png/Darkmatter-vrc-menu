import struct, os

# ── Read libmain.so ──────────────────────────────────────────────────────────
with open("unpacked/lib/arm64-v8a/libmain.so", "rb") as f:
    data = bytearray(f.read())

# ── ELF helpers ──────────────────────────────────────────────────────────────
def read_u64(d, off): return struct.unpack_from("<Q", d, off)[0]
def read_u32(d, off): return struct.unpack_from("<I", d, off)[0]
def read_u16(d, off): return struct.unpack_from("<H", d, off)[0]
def write_u64(d, off, v): struct.pack_into("<Q", d, off, v)
def write_u32(d, off, v): struct.pack_into("<I", d, off, v)

e_shoff     = read_u64(data, 40)
e_shentsize = read_u16(data, 58)
e_shnum     = read_u16(data, 60)
e_shstrndx  = read_u16(data, 62)

# Get section name string table
shstr_off = e_shoff + e_shstrndx * e_shentsize
shstr_fileoff = read_u64(data, shstr_off + 24)

def sh_name(i):
    off = e_shoff + i * e_shentsize
    name_idx = read_u32(data, off)
    end = data.index(b"\x00", shstr_fileoff + name_idx)
    return data[shstr_fileoff + name_idx:end].decode("utf-8", errors="replace")

def sh_info(i):
    off = e_shoff + i * e_shentsize
    return {
        "name": sh_name(i),
        "type": read_u32(data, off + 4),
        "flags": read_u64(data, off + 8),
        "addr": read_u64(data, off + 16),
        "offset": read_u64(data, off + 24),
        "size": read_u64(data, off + 32),
        "entsize": read_u64(data, off + 56),
    }

sections = {sh_name(i): sh_info(i) for i in range(e_shnum)}

# ── Find .dynstr section — add our lib name ───────────────────────────────────
dynstr = sections.get(".dynstr")
if not dynstr:
    raise RuntimeError(".dynstr not found")

dynstr_data = data[dynstr["offset"]:dynstr["offset"] + dynstr["size"]]
our_lib = b"libdarkmatter.so\x00"

# Check if already there
if our_lib in dynstr_data:
    lib_str_offset = dynstr_data.index(our_lib)
    print(f"libdarkmatter.so already in .dynstr at +{lib_str_offset}")
else:
    # Append to end of .dynstr — we need to extend it
    # Simple approach: find null padding after .dynstr and use it
    lib_str_offset = dynstr["size"]
    write_pos = dynstr["offset"] + dynstr["size"]
    
    # Check there's room (there usually is padding)
    if data[write_pos:write_pos + len(our_lib)] == b"\x00" * len(our_lib):
        data[write_pos:write_pos + len(our_lib)] = our_lib
        # Update .dynstr size in section header
        dynstr_sh_off = None
        for i in range(e_shnum):
            if sh_name(i) == ".dynstr":
                dynstr_sh_off = e_shoff + i * e_shentsize
                break
        if dynstr_sh_off:
            write_u64(data, dynstr_sh_off + 32, dynstr["size"] + len(our_lib))
        print(f"Added libdarkmatter.so to .dynstr at +{lib_str_offset}")
    else:
        print("WARNING: no padding after .dynstr — using dlopen string injection fallback")
        lib_str_offset = None

# ── Add DT_NEEDED entry for libdarkmatter.so in .dynamic ─────────────────────
dynamic = sections.get(".dynamic")
if not dynamic or lib_str_offset is None:
    print("Falling back to string table injection only")
else:
    # Walk .dynamic entries, find DT_NULL (0) terminator, insert DT_NEEDED before it
    dyn_off = dynamic["offset"]
    dyn_size = dynamic["size"]
    entsize = dynamic["entsize"] or 16  # each entry is 16 bytes (tag + val)
    
    dt_null_pos = None
    for i in range(0, dyn_size, entsize):
        tag = read_u64(data, dyn_off + i)
        if tag == 0:  # DT_NULL
            dt_null_pos = dyn_off + i
            break
    
    if dt_null_pos:
        # Shift DT_NULL one slot forward and insert DT_NEEDED
        # First check the slot after DT_NULL is zeroed (safe to extend)
        if dt_null_pos + entsize + entsize <= dyn_off + dyn_size:
            # Move DT_NULL forward
            data[dt_null_pos + entsize:dt_null_pos + entsize * 2] = data[dt_null_pos:dt_null_pos + entsize]
            # Write DT_NEEDED (tag=1) with our string offset
            struct.pack_into("<QQ", data, dt_null_pos, 1, lib_str_offset)
            print(f"DT_NEEDED entry added for libdarkmatter.so (str_offset={lib_str_offset})")
        else:
            print("WARNING: no room to extend .dynamic")
    else:
        print("WARNING: DT_NULL not found in .dynamic")

with open("unpacked/lib/arm64-v8a/libmain.so", "wb") as f:
    f.write(data)
print("libmain.so patched with DT_NEEDED for libdarkmatter.so")
