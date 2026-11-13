import zipfile, os, hashlib, base64, datetime, struct
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

# ── Package names ────────────────────────────────────────────────────────────
# Patched VRChat keeps its original package so it installs as VRChat
VRCHAT_PKG  = "com.vrchat.oculus.quest"
# Module gets its own unique package so Android installs it as a separate app
MODULE_PKG  = "com.darkmatter.menu"

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

def swap_string_utf16le(data, old_str, new_str):
    """Find old_str encoded as UTF-16-LE in data and swap to new_str in-place."""
    old_b = old_str.encode("utf-16-le")
    new_b = new_str.encode("utf-16-le")
    idx = 0
    count = 0
    while True:
        pos = data.find(old_b, idx)
        if pos == -1:
            break
        # Pad or truncate to same byte length as old
        if len(new_b) <= len(old_b):
            padded = new_b + b"\x00\x00" * ((len(old_b) - len(new_b)) // 2)
            data[pos:pos+len(old_b)] = padded
        else:
            # Extend — just overwrite, may corrupt adjacent data if too different
            data[pos:pos+len(new_b)] = new_b
        # Patch the UTF-16-LE length field (uint16 2 bytes before string)
        len_pos = pos - 2
        if len_pos >= 0:
            struct.pack_into("<H", data, len_pos, len(new_str))
        count += 1
        idx = pos + max(len(old_b), len(new_b))
    return count

# ── Build module APK ─────────────────────────────────────────────────────────
print(f"Building module APK — package: {MODULE_PKG}")

with zipfile.ZipFile("VRChat.apk") as z:
    manifest_data = bytearray(z.read("AndroidManifest.xml"))

# Swap every occurrence of the VRChat package name to our module package
n = swap_string_utf16le(manifest_data, VRCHAT_PKG, MODULE_PKG)
print(f"Swapped package string {n}x: {VRCHAT_PKG} -> {MODULE_PKG}")

# Verify the swap worked
if MODULE_PKG.encode("utf-16-le") in manifest_data:
    print(f"VERIFIED: {MODULE_PKG} present in binary manifest")
else:
    print("WARNING: package swap may have failed")

# Also swap the app label to Darkmatter Menu
swap_string_utf16le(manifest_data, "VRChat", "Darkmatter Menu")

# Pick the .so payload
so_path = None
for root, dirs, files in os.walk("."):
    for fn in files:
        if fn == "libdarkmatter.so":
            so_path = os.path.join(root, fn)
            break
    if so_path: break
if not so_path:
    so_path = "unpacked/lib/arm64-v8a/libvrc_fast_crypto.so"
    print(f"Payload: patched libvrc_fast_crypto.so ({os.path.getsize(so_path)//1024}KB)")

with zipfile.ZipFile("darkmatter-module-unsigned.apk", "w", zipfile.ZIP_DEFLATED) as z:
    info_mf = zipfile.ZipInfo("AndroidManifest.xml")
    info_mf.compress_type = zipfile.ZIP_STORED
    z.writestr(info_mf, bytes(manifest_data))

    z.writestr("assets/xposed_init", f"{MODULE_PKG}.XposedInit\n".encode())

    info_so = zipfile.ZipInfo("lib/arm64-v8a/libdarkmatter.so")
    info_so.compress_type = zipfile.ZIP_STORED
    with open(so_path, "rb") as f:
        z.writestr(info_so, f.read())

sign_apk("darkmatter-module-unsigned.apk", "darkmatter-module.apk")
size = os.path.getsize("darkmatter-module.apk")
print(f"darkmatter-module.apk: {size//1024}KB — package: {MODULE_PKG}")
