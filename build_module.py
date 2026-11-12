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

# ── Build binary AndroidManifest.xml using aapt2 ────────────────────────────
print("Compiling AndroidManifest with aapt2...")

# Write a proper Gradle-free minimal Android project for aapt2
os.makedirs("module_build/res", exist_ok=True)

manifest_text = """<?xml version="1.0" encoding="utf-8"?>
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.darkmatter.vrc"
    android:versionCode="1"
    android:versionName="1.0">
    <uses-sdk android:minSdkVersion="29" android:targetSdkVersion="32"/>
    <application android:label="Darkmatter VRC" android:hasCode="false">
        <meta-data android:name="xposedmodule" android:value="true"/>
        <meta-data android:name="xposeddescription" android:value="Darkmatter VRChat mod menu"/>
        <meta-data android:name="xposedminversion" android:value="93"/>
    </application>
</manifest>"""

with open("module_build/AndroidManifest.xml", "w") as f:
    f.write(manifest_text)

# Use aapt2 to compile and link into a flat APK with binary manifest
import subprocess, shutil

aapt2 = shutil.which("aapt2") or "/usr/local/lib/android/sdk/build-tools/35.0.0/aapt2"

# Try aapt2 link to get binary manifest
result = subprocess.run([
    aapt2, "link",
    "--manifest", "module_build/AndroidManifest.xml",
    "-o", "module_build/base.apk",
    "--min-sdk-version", "29",
    "--target-sdk-version", "32",
    "--version-code", "1",
    "--version-name", "1.0",
    "--java", "module_build/",
], capture_output=True, text=True)

if result.returncode != 0:
    print("aapt2 error:", result.stderr[:500])
    # Fallback: use apktools approach — download minimal framework
    # Actually use the Android framework from the NDK install
    android_jar = None
    import glob
    for p in glob.glob("/usr/local/lib/android/sdk/platforms/*/android.jar"):
        android_jar = p
        break
    if android_jar:
        result2 = subprocess.run([
            aapt2, "link",
            "--manifest", "module_build/AndroidManifest.xml",
            "-I", android_jar,
            "-o", "module_build/base.apk",
        ], capture_output=True, text=True)
        print("aapt2 with framework:", result2.returncode, result2.stderr[:200])
    else:
        print("No android.jar found, trying apt install...")
        subprocess.run(["apt-get", "install", "-y", "aapt2"], capture_output=True)
else:
    print("aapt2 link OK")

# Extract binary manifest from the linked APK
if os.path.exists("module_build/base.apk"):
    with zipfile.ZipFile("module_build/base.apk") as z:
        binary_manifest = z.read("AndroidManifest.xml")
    print(f"Binary manifest: {len(binary_manifest)} bytes")
else:
    print("FALLBACK: using plain text manifest (may not parse package name)")
    binary_manifest = manifest_text.encode()

# ── Assemble module APK ──────────────────────────────────────────────────────
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
    z.writestr("AndroidManifest.xml", binary_manifest)
    z.writestr("assets/xposed_init", b"com.darkmatter.vrc.XposedInit\n")
    info = zipfile.ZipInfo("lib/arm64-v8a/libdarkmatter.so")
    info.compress_type = zipfile.ZIP_STORED
    with open(so_path,"rb") as f:
        z.writestr(info, f.read())

sign_apk("darkmatter-module-unsigned.apk", "darkmatter-module.apk")
size = os.path.getsize("darkmatter-module.apk")
print(f"darkmatter-module.apk ready: {size//1024}KB")
