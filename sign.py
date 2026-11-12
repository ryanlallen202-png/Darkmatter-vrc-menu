import zipfile, os, hashlib, base64, datetime, struct
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

SIGNED_PKG = os.environ.get("SIGNED_PKG", "com.darkmatter.vrcmenu.signed")

WRAP_SH = b"#!/system/bin/sh\n" \
          b"HERE=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n" \
          b"export LD_PRELOAD=\"$HERE/libdarkmatter.so${LD_PRELOAD:+:$LD_PRELOAD}\"\n" \
          b"if [ -f \"$HERE/libgadget.so\" ]; then\n" \
          b"    export LD_PRELOAD=\"$HERE/libgadget.so:$LD_PRELOAD\"\n" \
          b"fi\n" \
          b"exec \"$@\"\n"

def patch_manifest(data_bytes, new_package):
    data    = bytearray(data_bytes)
    old_pkg = "com.vrchat.oculus.quest"
    old_b   = old_pkg.encode("utf-16-le")
    new_b   = new_package.encode("utf-16-le")
    old_len = len(old_pkg)
    new_len = len(new_package)
    idx = data.find(old_b)
    if idx != -1:
        if new_len <= old_len:
            padded = new_b + b"\x00\x00" * (old_len - new_len)
            data[idx:idx + len(old_b)] = padded
            struct.pack_into("<H", data, idx - 2, new_len)
            if idx >= 4: struct.pack_into("<H", data, idx - 4, new_len)
        else:
            data[idx:idx + len(new_b)] = new_b
            struct.pack_into("<H", data, idx - 2, new_len)
        print(f"Patched package -> {new_package}")
    ah = data.find(bytes([0xbe, 0x00, 0x01, 0x01]))
    if ah != -1:
        vo = ah + 16
        if vo + 4 <= len(data) and struct.unpack_from("<I", data, vo)[0] == 0:
            struct.pack_into("<I", data, vo, 1)
            print("Patched extractNativeLibs -> true")
    return bytes(data)

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
nm  = x509.Name([
    x509.NameAttribute(NameOID.COMMON_NAME, "DarkMatter"),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, "olivia"),
])
cert = (x509.CertificateBuilder()
    .subject_name(nm).issuer_name(nm).public_key(key.public_key())
    .serial_number(x509.random_serial_number())
    .not_valid_before(datetime.datetime(2024,1,1))
    .not_valid_after(datetime.datetime(2034,1,1))
    .sign(key, hashes.SHA256()))

src = "VRChat.apk"
dst = "DarkMatter-patched-signed.apk"
STORE_EXTS = {".so", ".arsc", ".png", ".jpg", ".flat"}

manifest_lines = ["Manifest-Version: 1.0\r\nCreated-By: ollie\r\n\r\n"]
digests = {}

with zipfile.ZipFile(src, "r") as zin:
    for item in zin.infolist():
        if item.filename.startswith("META-INF/"): continue
        if item.filename == "lib/arm64-v8a/libvrc_fast_crypto.so":
            d = open("unpacked/lib/arm64-v8a/libvrc_fast_crypto.so","rb").read()
        elif item.filename == "AndroidManifest.xml":
            d = patch_manifest(zin.read(item.filename), SIGNED_PKG)
        else:
            d = zin.read(item.filename)
        sha = base64.b64encode(hashlib.sha256(d).digest()).decode()
        manifest_lines.append(f"Name: {item.filename}\r\nSHA-256-Digest: {sha}\r\n\r\n")
        digests[item.filename] = (sha, d)

# sign wrap.sh as part of the APK
wrap_sha = base64.b64encode(hashlib.sha256(WRAP_SH).digest()).decode()
manifest_lines.append(f"Name: lib/arm64-v8a/wrap.sh\r\nSHA-256-Digest: {wrap_sha}\r\n\r\n")
digests["lib/arm64-v8a/wrap.sh"] = (wrap_sha, WRAP_SH)

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
            item.compress_type = zipfile.ZIP_STORED if ext in STORE_EXTS or item.filename == "resources.arsc" else zipfile.ZIP_DEFLATED
            zout.writestr(item, d)
        info_ws = zipfile.ZipInfo("lib/arm64-v8a/wrap.sh")
        info_ws.compress_type = zipfile.ZIP_STORED
        zout.writestr(info_ws, WRAP_SH)
        zout.writestr("META-INF/MANIFEST.MF", manifest)
        zout.writestr("META-INF/CERT.SF", sf)
        zout.writestr("META-INF/CERT.RSA", p7)

print(f"{dst}: {os.path.getsize(dst)//1024//1024}MB  package={SIGNED_PKG}")
print("[+] wrap.sh injected — sideload direct, no root needed")
