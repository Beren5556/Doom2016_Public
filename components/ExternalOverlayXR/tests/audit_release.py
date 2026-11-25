"""Audit operational sources and a consumer kit without loading a VR runtime."""
from pathlib import Path
import argparse
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
FORBIDDEN = ("ofxr", "djules75", "xrframebridge", "rtw77")
SOURCE_DIRS = ("native", "host", "examples", "third_party", "scripts")
SOURCE_FILES = ("CMakeLists.txt", "VERSION", "ExternalOverlayXR.json", "package.json", "requirements.txt")
KIT_DIRS = ("host", "examples", "licenses", "docs")
KIT_FILES = ("ExternalOverlayXR.dll", "ExternalOverlayXR.json", "VERSION", "package.json",
             "requirements.txt", "LICENSE", "THIRD_PARTY.md", "README.md", "SHA256SUMS.json")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def scan(path, name):
    data = path.read_bytes().lower()
    for token in FORBIDDEN:
        if token in name.lower() or token.encode() in data or token.encode("utf-16le") in data:
            raise ValueError(f"Excluded provider marker {token!r} in {name}")


def source_audit(root):
    files = [root / p for p in SOURCE_FILES]
    for folder in SOURCE_DIRS:
        files += [p for p in (root / folder).rglob("*")
                  if p.is_file() and "__pycache__" not in p.parts and p.suffix != ".pyc"]
    for path in files:
        if path.is_symlink():
            raise ValueError(f"Unexpected source link: {path}")
        scan(path, path.relative_to(root).as_posix())
    version = (root / "VERSION").read_text().strip()
    assert json.loads((root / "package.json").read_text())["version"] == version
    return {"files": len(files), "sha256": {p.relative_to(root).as_posix(): sha(p) for p in files}}


def kit_audit(root):
    files = [p for p in root.rglob("*") if p.is_file()]
    for path in files:
        name = path.relative_to(root).as_posix()
        top = path.relative_to(root).parts[0]
        if path.is_symlink() or not (top in KIT_DIRS or name in KIT_FILES):
            raise ValueError(f"Unexpected delivery entry: {name}")
        # Documentation can name removed features; all executable/input files
        # and every filename are checked. License text is retained unmodified.
        for token in FORBIDDEN:
            if token in name.lower():
                raise ValueError(f"Excluded delivery filename: {name}")
        if top not in ("docs", "licenses") and path.suffix.lower() != ".md":
            scan(path, name)
    expected = json.loads((root / "SHA256SUMS.json").read_text())
    actual = {p.relative_to(root).as_posix(): sha(p) for p in files if p.name != "SHA256SUMS.json"}
    if actual != expected:
        raise ValueError("Delivery file list or hashes differ from SHA256SUMS.json")
    for name in KIT_FILES:
        if not (root / name).is_file():
            raise ValueError(f"Missing delivery file: {name}")
    manifest = json.loads((root / "ExternalOverlayXR.json").read_text())["api_layer"]
    assert manifest["name"] == "XR_APILAYER_EXTERNAL_control_overlay"
    assert manifest["library_path"] == ".\\ExternalOverlayXR.dll"
    assert manifest["implementation_version"] == "2"
    assert (root / "VERSION").read_text().strip() == "0.1.1"
    return {"files": len(files), "dllSHA256": sha(root / "ExternalOverlayXR.dll"), "sha256": actual}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT)
    parser.add_argument("--kit", type=Path)
    parser.add_argument("--receipt", type=Path)
    args = parser.parse_args()
    result = {"result": "PASS", "source": source_audit(args.source.resolve())}
    if args.kit:
        result["kit"] = kit_audit(args.kit.resolve())
    if args.receipt:
        args.receipt.parent.mkdir(parents=True, exist_ok=True)
        args.receipt.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"PASS operational sources: {result['source']['files']} files"
          + (f"; delivery: {result['kit']['files']} files" if args.kit else ""))
