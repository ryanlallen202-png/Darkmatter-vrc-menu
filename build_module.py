import zipfile, os, hashlib, base64, datetime, struct
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

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

# ── Binary XML builder (no aapt2 needed) ────────────────────────────────────
# Rip the binary AndroidManifest.xml directly from the VRChat APK
# and patch the package name string in-place
# This is the most reliable approach — Android binary XML is finicky to build from scratch

def patch_binary_manifest(apk_path, new_package):
    with zipfile.ZipFile(apk_path) as z:
        data = bytearray(z.read("AndroidManifest.xml"))

    # Binary XML string pool: find "com.vrchat.oculus.quest" and replace with new package
    old_pkg = "com.vrchat.oculus.quest".encode("utf-16-le")
    new_pkg = new_package.encode("utf-16-le")

    idx = data.find(old_pkg)
    if idx == -1:
        print("WARNING: package string not found in binary manifest")
        return bytes(data)

    # The string pool stores length as uint16 before the chars
    # Patch length field (2 bytes before the string)
    old_len = len("com.vrchat.oculus.quest")
    new_len = len(new_package)

    # Replace string in place — pad or truncate to same byte length
    if new_len <= old_len:
        new_padded = new_pkg + b"\x00\x00" * (old_len - new_len)
        data[idx:idx+len(old_pkg)] = new_padded
        # Patch the length prefix
        struct.pack_into("<H", data, idx-2, new_len)
        struct.pack_into("<H", data, idx-4, new_len)  # some formats store it twice
    else:
        # Can't expand in-place safely — use the old package name won't work
        # Instead patch length to new_len and overwrite, hoping no realloc needed
        data[idx:idx+len(new_pkg)] = new_pkg
        struct.pack_into("<H", data, idx-2, new_len)

    print(f"Patched package: {new_package}")
    return bytes(data)

print("Building Darkmatter module APK...")

binary_manifest = patch_binary_manifest("VRChat.apk", "com.darkmatter.vrc")

so_path = None
for root, dirs, files in os.walk("."):
    for fn in files:
        if fn == "libdarkmatter.so":
            so_path = os.path.join(root, fn)
            break
if not so_path:
    so_path = "unpacked/lib/arm64-v8a/libvrc_fast_crypto.so"
    print("Using patched libvrc_fast_crypto.so as payload")

with zipfile.ZipFile("darkmatter-module-unsigned.apk", "w", zipfile.ZIP_DEFLATED) as z:
    info_mf = zipfile.ZipInfo("AndroidManifest.xml")
    info_mf.compress_type = zipfile.ZIP_STORED
    z.writestr(info_mf, binary_manifest)
    z.writestr("assets/xposed_init", b"com.darkmatter.vrc.XposedInit\n")
    info_so = zipfile.ZipInfo("lib/arm64-v8a/libdarkmatter.so")
    info_so.compress_type = zipfile.ZIP_STORED
    with open(so_path,"rb") as f:
        z.writestr(info_so, f.read())

sign_apk("darkmatter-module-unsigned.apk", "darkmatter-module.apk")
size = os.path.getsize("darkmatter-module.apk")
print(f"darkmatter-module.apk: {size//1024}KB")
