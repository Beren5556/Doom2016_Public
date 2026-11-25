"""Actual native startup -> hidden host -> browser pixels -> automatic cleanup. No XR/game."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
kernel = ctypes.WinDLL('kernel32', use_last_error=True)
kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
kernel.OpenProcess.restype = wintypes.HANDLE
kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
kernel.CloseHandle.argtypes = [wintypes.HANDLE]

for mode in (2, 1):
    with tempfile.TemporaryDirectory(prefix='doom-layer-bootstrap-') as temporary:
        fixture = Path(temporary)
        exe = fixture / 'startup.exe'
        shutil.copy2(sys.argv[1], exe)
        root = fixture / 'DoomLayer'
        shutil.copytree(ROOT, root, ignore=shutil.ignore_patterns('__pycache__', 'tests'))
        (root / 'enabled').touch()
        (root / 'startup.cfg').write_text(f'1 {mode} 11 1.3\n', encoding='utf-8')
        (root / 'python.cfg').write_text(sys.executable, encoding='utf-8')
        (root / 'runtime.json').write_text(json.dumps(dict(node=os.environ['NODE_EXE'], modules=os.environ['NODE_PATH'])), encoding='utf-8')
        (root / 'native').mkdir()
        # prepareXr checks staged paths. This test never loads a runtime or calls xrCreateInstance.
        for name in ('ExternalOverlayXR.json', 'ExternalOverlayXR.dll'):
            (root / 'native' / name).touch()
        env = os.environ.copy()
        env.update(KHARVOX_DISABLE_AA='1', KHARVOX_DLSS_DLAA='0' if mode==2 else '1')
        result = subprocess.run([str(exe), '--isolated-fixture'], env=env, capture_output=True, text=True, timeout=22)
        receipts = list((root / 'logs').glob('processes-*.json'))
        assert receipts, result.stdout + result.stderr + '\n' + '\n'.join(p.read_text() for p in (root/'logs').glob('*.log'))
        identities = json.loads(receipts[0].read_text())
        handles = [kernel.OpenProcess(0x100000, False, identities[key]) for key in ('host', 'renderer')]
        try:
            assert all(not handle or kernel.WaitForSingleObject(handle, 8000)==0 for handle in handles), 'Orphan host/browser after fixture exit'
        finally:
            for handle in handles:
                if handle: kernel.CloseHandle(handle)
        evidence = Path(sys.argv[2]) / f'bootstrap-mode-{mode}'
        evidence.mkdir(parents=True, exist_ok=True)
        shutil.copytree(root / 'logs', evidence / 'logs', dirs_exist_ok=True)
        (evidence / 'result.json').write_text(json.dumps(dict(exitCode=result.returncode, stdout=result.stdout,
            stderr=result.stderr, processes=identities, cleanupPassed=True, gameOrVrStarted=False), indent=2), encoding='utf-8')
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout.strip())
print('PASS native authority in both modes, real browser surface, automatic lifetime cleanup; no game/VR launched')
