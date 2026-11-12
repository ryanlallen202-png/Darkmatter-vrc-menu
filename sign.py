import zipfile, os, hashlib, base64, datetime
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography import x509
from cryptography.x509.oid import NameOID

key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
name = x509.Name([
    x509.NameAttribute(NameOID.COMMON_NAME, "DarkMatter"),
    x509.NameAttribute(NameOID.ORGANIZATION_NAME, "olivia"),
])
cert = (x509.CertificateBuilder()
    .subject_name(name).issuer_name(name)
    .public_key(key.public_key())
    .serial_number(x509.random_serial_number())
    .not_valid_before(datetime.datetime(2024,1,1))
    .not_valid_after(datetime.datetime(2034,1,1))
    .sign(key, hashes.SHA256()))

src = "VRChat.apk"
dst = "DarkMatter-patched-signed.apk"

# Files that MUST be stored uncompressed + 4-byte aligned (Android R+ requirement)
MUST_STORE = {".so", ".arsc"}

manifest_lines = ["Manifest-Version: 1.0\r\nCreated-By: ollie\r\n\r\n"]
digests = {}

with zipfile.ZipFile(src, "r") as zin:
    for item in zin.infolist():
        if item.filename.startswith("META-INF/"): continue
        if item.filename == "lib/arm64-v8a/libvrc_fast_crypto.so":
            d = open("unpacked/lib/arm64-v8a/libvrc_fast_crypto.so","rb").read()
        else:
            d = zin.read(item.filename)
        sha = base64.b64encode(hashlib.sha256(d).digest()).decode()
        manifest_lines.append(f"Name: {item.filename}\r\nSHA-256-Digest: {sha}\r\n\r\n")
        digests[item.filename] = (sha, d)

manifest = "".join(manifest_lines).encode()
sf_lines = ["Signature-Version: 1.0\r\nCreated-By: ollie\r\nSHA-256-Digest-Manifest: "
            + base64.b64encode(hashlib.sha256(manifest).digest()).decode() + "\r\n\r\n"]
for n, (s, _) in digests.items():
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
            if ext in MUST_STORE or item.filename == "resources.arsc":
                item.compress_type = zipfile.ZIP_STORED
            else:
                item.compress_type = zipfile.ZIP_DEFLATED
            zout.writestr(item, d)
        zout.writestr("META-INF/MANIFEST.MF", manifest)
        zout.writestr("META-INF/CERT.SF", sf)
        zout.writestr("META-INF/CERT.RSA", p7)

print(f"Signed: {dst} ({os.path.getsize(dst)//1024//1024}MB)")
