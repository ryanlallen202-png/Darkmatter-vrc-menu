import zipfile, os, hashlib, base64, datetime, struct
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

# ── Modded VRChat gets its own package so it installs ALONGSIDE real VRChat ──
MODDED_PKG   = "com.darkmatter.vrchat"   # installs as separate app
ORIGINAL_PKG = "com.vrchat.oculus.quest"

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
    STORE_EXTS = {".so", ".arsc"}
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
                item.compress_type = zipfile.ZIP_STORED if ext in STORE_EXTS else zipfile.ZIP_DEFLATED
                zout.writestr(item, d)
            zout.writestr("META-INF/MANIFEST.MF", manifest)
            zout.writestr("META-INF/CERT.SF", sf)
            zout.writestr("META-INF/CERT.RSA", p7)

def swap_pkg_in_manifest(apk_path, old_pkg, new_pkg):
    with zipfile.ZipFile(apk_path) as z:
        data = bytearray(z.read("AndroidManifest.xml"))
    old_b = old_pkg.encode("utf-16-le")
    new_b = new_pkg.encode("utf-16-le")
    idx = 0
    count = 0
    while True:
        pos = data.find(old_b, idx)
        if pos == -1: break
        # pad new to same byte length as old
        if len(new_b) <= len(old_b):
            padded = new_b + b"\x00\x00" * ((len(old_b) - len(new_b)) // 2)
            data[pos:pos+len(old_b)] = padded
        else:
            data[pos:pos+len(new_b)] = new_b
        # patch length prefix
        if pos >= 2:
            struct.pack_into("<H", data, pos-2, len(new_pkg))
        count += 1
        idx = pos + len(old_b)
    print(f"Package swap {old_pkg} -> {new_pkg} ({count}x)")
    return bytes(data)

# ── Build modded VRChat APK with new package name ────────────────────────────
print(f"Building modded VRChat — package: {MODDED_PKG}")
patched_manifest = swap_pkg_in_manifest("VRChat.apk", ORIGINAL_PKG, MODDED_PKG)

# Verify swap worked
if MODDED_PKG.encode("utf-16-le") in patched_manifest:
    print(f"VERIFIED: {MODDED_PKG} in manifest")
else:
    print("WARNING: package swap may have failed — check manifest")

# Repack with patched lib + patched manifest
with zipfile.ZipFile("VRChat.apk", "r") as zin:
    with zipfile.ZipFile("DarkMatter-modded-unsigned.apk", "w", allowZip64=True) as zout:
        for item in zin.infolist():
            if item.filename.startswith("META-INF/"): continue
            if item.filename == "AndroidManifest.xml":
                info = zipfile.ZipInfo("AndroidManifest.xml")
                info.compress_type = zipfile.ZIP_STORED
                zout.writestr(info, patched_manifest)
                continue
            if item.filename == "lib/arm64-v8a/libvrc_fast_crypto.so":
                data = open("unpacked/lib/arm64-v8a/libvrc_fast_crypto.so","rb").read()
            else:
                data = zin.read(item.filename)
            ext = os.path.splitext(item.filename)[1].lower()
            item.compress_type = zipfile.ZIP_STORED if ext in {".so",".arsc"} else zipfile.ZIP_DEFLATED
            zout.writestr(item, data)

sign_apk("DarkMatter-modded-unsigned.apk", "DarkMatter-modded-signed.apk")
print(f"DarkMatter-modded-signed.apk: {os.path.getsize('DarkMatter-modded-signed.apk')//1024//1024}MB — package: {MODDED_PKG}")
