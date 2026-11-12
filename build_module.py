import zipfile, os, hashlib, base64, datetime, struct
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

MODULE_PKG = os.environ.get("MODULE_PKG", "com.darkmatter.module")

# wrap.sh — the entire no-root mechanism.
# Android executes this before the app process when wrap.sh
# exists at lib/<abi>/wrap.sh and extractNativeLibs=true.
# We LD_PRELOAD libdarkmatter.so so it loads into VRChat cold.
WRAP_SH = b"#!/system/bin/sh\n" \
          b"HERE=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n" \
          b"export LD_PRELOAD=\"$HERE/libdarkmatter.so${LD_PRELOAD:+:$LD_PRELOAD}\"\n" \
          b"if [ -f \"$HERE/libgadget.so\" ]; then\n" \
          b"    export LD_PRELOAD=\"$HERE/libgadget.so:$LD_PRELOAD\"\n" \
          b"fi\n" \
          b"exec \"$@\"\n"

def sign_apk(src, dst):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    nm  = x509.Name([
        x509.NameAttribute(NameOID.COMMON_NAME, "DarkMatter"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "olivia"),
    ])
    cert = (x509.CertificateBuilder()
        .subject_name(nm).issuer_name(nm)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.datetime(2024,1,1))
        .not_valid_after(datetime.datetime(2034,1,1))
        .sign(key, hashes.SHA256()))
    manifest_lines = ["Manifest-Version: 1.0\r\nCreated-By: ollie\r\n\r\n"]
    digests = {}
    with zipfile.ZipFile(src, "r") as zin:
        for item in zin.infolist():
            if item.filename.startswith("META-INF/"): continue
            d = zin.read(item.filename)
            sha = base64.b64encode(hashlib.sha256(d).digest()).decode()
            manifest_lines.append(f"Name: {item.filename}\r\nSHA-256-Digest: {sha}\r\n\r\n")
            digests[item.filename] = (sha, d)
    manifest = "".join(manifest_lines).encode()
    sf_lines = ["Signature-Version: 1.0\r\nCreated-By: ollie\r\nSHA-256-Digest-Manifest: "
                + base64.b64encode(hashlib.sha256(manifest).digest()).decode() + "\r\n\r\n"]
    for n,(s,_) in digests.items():
        sf_lines.append(f"Name: {n}\r\nSHA-256-Digest: {s}\r\n\r\n")
    sf = "".join(sf_lines).encode()
    p7 = (pkcs7.PKCS7SignatureBuilder()
          .set_data(sf).add_signer(cert, key, hashes.SHA256())
          .sign(serialization.Encoding.DER, [pkcs7.PKCS7Options.DetachedSignature]))
    with zipfile.ZipFile(src, "r") as zin:
        with zipfile.ZipFile(dst, "w", allowZip64=True) as zout:
            for item in zin.infolist():
                if item.filename.startswith("META-INF/"): continue
                _, d = digests[item.filename]
                ext = os.path.splitext(item.filename)[1].lower()
                item.compress_type = zipfile.ZIP_STORED if ext in {".so",".arsc"} else zipfile.ZIP_DEFLATED
                zout.writestr(item, d)
            zout.writestr("META-INF/MANIFEST.MF", manifest)
            zout.writestr("META-INF/CERT.SF", sf)
            zout.writestr("META-INF/CERT.RSA", p7)

def patch_binary_manifest(apk_path, new_package):
    with zipfile.ZipFile(apk_path) as z:
        data = bytearray(z.read("AndroidManifest.xml"))
    old_pkg   = "com.vrchat.oculus.quest"
    old_bytes = old_pkg.encode("utf-16-le")
    new_bytes = new_package.encode("utf-16-le")
    old_len   = len(old_pkg)
    new_len   = len(new_package)
    idx = data.find(old_bytes)
    if idx == -1:
        print("WARNING: package string not found")
    else:
        if new_len <= old_len:
            padded = new_bytes + b"\x00\x00" * (old_len - new_len)
            data[idx:idx + len(old_bytes)] = padded
            struct.pack_into("<H", data, idx - 2, new_len)
            if idx >= 4: struct.pack_into("<H", data, idx - 4, new_len)
        else:
            data[idx:idx + len(new_bytes)] = new_bytes
            struct.pack_into("<H", data, idx - 2, new_len)
        print(f"Patched package -> {new_package}")
    # flip extractNativeLibs false->true (attr hash 0x010100be)
    ah = data.find(bytes([0xbe, 0x00, 0x01, 0x01]))
    if ah != -1:
        vo = ah + 16
        if vo + 4 <= len(data) and struct.unpack_from("<I", data, vo)[0] == 0:
            struct.pack_into("<I", data, vo, 1)
            print("Patched extractNativeLibs -> true")
    else:
        print("WARN: extractNativeLibs attr not found by hash")
    return bytes(data)

print(f"Building no-root Darkmatter APK — package: {MODULE_PKG}")
binary_manifest = patch_binary_manifest("VRChat.apk", MODULE_PKG)

so_path = None
for root, dirs, files in os.walk("."):
    for fn in files:
        if fn == "libdarkmatter.so":
            so_path = os.path.join(root, fn)
            break
    if so_path: break
if not so_path:
    so_path = "unpacked/lib/arm64-v8a/libvrc_fast_crypto.so"
    print("Fallback: patched libvrc_fast_crypto.so")

print(f"Payload: {so_path} ({os.path.getsize(so_path)//1024}KB)")

with zipfile.ZipFile("darkmatter-module-unsigned.apk", "w", zipfile.ZIP_DEFLATED) as z:
    info_mf = zipfile.ZipInfo("AndroidManifest.xml")
    info_mf.compress_type = zipfile.ZIP_STORED
    z.writestr(info_mf, binary_manifest)
    z.writestr("assets/xposed_init", (MODULE_PKG + ".XposedInit\n").encode())
    info_ws = zipfile.ZipInfo("lib/arm64-v8a/wrap.sh")
    info_ws.compress_type = zipfile.ZIP_STORED
    z.writestr(info_ws, WRAP_SH)
    print("[+] wrap.sh injected — no-root LD_PRELOAD hook")
    info_so = zipfile.ZipInfo("lib/arm64-v8a/libdarkmatter.so")
    info_so.compress_type = zipfile.ZIP_STORED
    with open(so_path, "rb") as f:
        z.writestr(info_so, f.read())
    if os.path.exists("frida-gadget.so"):
        info_fg = zipfile.ZipInfo("lib/arm64-v8a/libgadget.so")
        info_fg.compress_type = zipfile.ZIP_STORED
        with open("frida-gadget.so", "rb") as f:
            z.writestr(info_fg, f.read())
        print("[+] Frida gadget injected")

sign_apk("darkmatter-module-unsigned.apk", "darkmatter-module.apk")
print(f"darkmatter-module.apk: {os.path.getsize('darkmatter-module.apk')//1024}KB")
print("[+] Sideload this APK directly — no root, no LSPosed needed")
