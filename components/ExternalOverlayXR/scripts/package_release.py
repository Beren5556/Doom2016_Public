"""Build an allowlisted consumer kit from the already compiled release DLL."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, required=True)
parser.add_argument("--output", type=Path, default=ROOT / "dist")
args = parser.parse_args()
version = (ROOT / "VERSION").read_text().strip()
dll = args.build.resolve() / "native/Release/ExternalOverlayXR.dll"
assert dll.is_file(), dll
target = args.output.resolve() / f"ExternalOverlayXR-{version}"
archive = target.with_suffix(target.suffix + ".zip")
if target.exists() or archive.exists():
    raise SystemExit("Refusing to overwrite a delivery; choose a new output directory")
subprocess.run([sys.executable, str(ROOT / "tests/audit_release.py")], check=True)
target.mkdir(parents=True)
shutil.copy2(dll, target / dll.name)
for name in ("VERSION", "ExternalOverlayXR.json", "requirements.txt", "package.json", "LICENSE", "THIRD_PARTY.md"):
    shutil.copy2(ROOT / name, target / name)
for name in ("host", "examples"):
    shutil.copytree(ROOT / name, target / name, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
shutil.copytree(ROOT / "third_party/openxr/LICENSES", target / "licenses/OpenXR")
(target / "docs").mkdir()
for name in ("SURFACE_ABI.md", "ADAPTER_API.md", "INTEGRATING_A_CONSUMER.md", "FIRST_INSTALL_AND_BOOTSTRAP.md"):
    shutil.copy2(ROOT / "docs" / name, target / "docs" / name)
(target / "docs/validation").mkdir()
shutil.copy2(ROOT / "docs/validation/first-install-20261005.json", target / "docs/validation/first-install-20261005.json")
shutil.copy2(ROOT / "docs/CONSUMER_KIT.md", target / "README.md")
manifest = {p.relative_to(target).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(target.rglob("*")) if p.is_file()}
(target / "SHA256SUMS.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
subprocess.run([sys.executable, str(ROOT / "tests/audit_release.py"), "--kit", str(target)], check=True)
with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as z:
    for p in sorted(target.rglob("*")):
        if p.is_file():
            z.write(p, p.relative_to(target).as_posix())
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(f"Created {archive.name}; DLL SHA256 {manifest['ExternalOverlayXR.dll']}")
