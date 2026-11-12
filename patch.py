import struct, os, urllib.request, zipfile, shutil

# ── 1. Patch libvrc_fast_crypto.so ──────────────────────────────────────────
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
print("libvrc_fast_crypto patched.")

# ── 2. Download Frida gadget arm64 ──────────────────────────────────────────
import gzip
print("Downloading Frida gadget...")
url = "https://github.com/frida/frida/releases/download/16.2.1/frida-gadget-16.2.1-android-arm64.so.gz"
urllib.request.urlretrieve(url, "gadget.so.gz")
with gzip.open("gadget.so.gz", "rb") as gz:
    gadget_data = gz.read()
with open("unpacked/lib/arm64-v8a/libfrida-gadget.so", "wb") as f:
    f.write(gadget_data)
print(f"Frida gadget: {len(gadget_data)//1024//1024}MB")

# ── 3. Write Frida gadget config — hook EOS tamper report ───────────────────
import json
config = {
    "interaction": {
        "type": "script",
        "path": "./libhook.js.so"
    }
}
with open("unpacked/lib/arm64-v8a/libfrida-gadget.config.so", "w") as f:
    json.dump(config, f)

# ── 4. Write the hook script ─────────────────────────────────────────────────
# Hook EOS_AntiCheat_Client_BeginSession and EOS_AntiCheat_Client_AddNotifyMessageToServer
# Both return EOS_Success (0) without doing anything
hook = r"""
'use strict';

function hookEOS() {
    const lib = Process.findModuleByName("libil2cpp.so");
    if (!lib) { setTimeout(hookEOS, 500); return; }

    // EOS anti-cheat exports — intercept tamper report sender
    const targets = [
        "EOS_AntiCheatClient_BeginSession",
        "EOS_AntiCheatClient_AddNotifyMessageToServer",
        "EOS_AntiCheatClient_ReceiveMessageFromServer",
        "EOS_AntiCheatClient_PollStatus",
    ];

    targets.forEach(name => {
        try {
            const exp = Module.findExportByName(null, name);
            if (exp) {
                Interceptor.attach(exp, {
                    onLeave(retval) { retval.replace(0); }
                });
                console.log("[ollie] hooked: " + name);
            }
        } catch(e) {}
    });

    // Hook the tamper detection string reporter in libil2cpp
    // Scan for the "Tampering Detected" string ref and NOP the branch
    const pattern = "54 61 6d 70 65 72 69 6e 67 20 44 65 74 65 63 74 65 64";
    Memory.scan(lib.base, lib.size, pattern, {
        onMatch(addr) {
            console.log("[ollie] Tampering string @ " + addr);
        }
    });

    // Hook VRC's own integrity reporter via dlsym
    try {
        const dl = new NativeFunction(Module.findExportByName(null, "dlsym"), "pointer", ["pointer","pointer"]);
        const rtld_default = ptr(0);
        const reportFn = dl(rtld_default, Memory.allocUtf8String("VRC_ReportTampering"));
        if (!reportFn.isNull()) {
            Interceptor.replace(reportFn, new NativeCallback(() => { return 0; }, "int", []));
            console.log("[ollie] VRC_ReportTampering stubbed");
        }
    } catch(e) {}
}

setTimeout(hookEOS, 100);
"""
with open("unpacked/lib/arm64-v8a/libhook.js.so", "w") as f:
    f.write(hook)

print("Hook script written.")

# ── 5. Patch libil2cpp to load Frida gadget on startup ──────────────────────
# Inject gadget load via patching the DT_NEEDED list is complex at 320MB
# Easier: patch the app_process init_array or use wrap sh
# For Quest, we use the wrap.sh method — write it to lib/arm64-v8a/wrap.sh
os.makedirs("unpacked/lib/arm64-v8a", exist_ok=True)
wrap = '#!/system/bin/sh\nLD_PRELOAD=/data/app/~~*/com.vrchat.oculus.quest*/lib/arm64/libfrida-gadget.so "$@"\n'
with open("unpacked/lib/arm64-v8a/wrap.sh", "w") as f:
    f.write(wrap)
print("wrap.sh written.")
