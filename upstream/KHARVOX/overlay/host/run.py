"""Attach the existing common panel surface to this DOOM process only."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import threading
import traceback
from server import make_server
from transport import LayerTransport, atomic_text
from surface import Surface
from child_job import ChildJob


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--pid', type=int, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    runtime = json.loads((root / 'runtime.json').read_bytes())
    surface = Surface(args.pid)
    server, url = make_server(LayerTransport(root, args.pid), surface=surface, ui_root=root / 'web')
    threading.Thread(target=server.serve_forever, daemon=True).start()
    env = os.environ.copy()
    env['NODE_PATH'] = runtime['modules']
    env.pop('OVERLAY_TEST_DURATION_MS', None)
    logdir = root / 'logs'
    logdir.mkdir(exist_ok=True)
    job = ChildJob()
    try:
        with (logdir / f'host-{args.pid}.log').open('w', encoding='utf-8') as log:
            child = subprocess.Popen([runtime['node'], str(root / 'host/render_surface.cjs'), url],
                                     env=env, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
            try:
                job.assign(child)
            except Exception:
                child.terminate()
                child.wait(timeout=5)
                raise
            atomic_text(logdir / f'processes-{args.pid}.json', json.dumps(dict(
                game=args.pid, host=os.getpid(), renderer=child.pid)))
            while child.poll() is None:
                if surface.kernel.WaitForSingleObject(surface.process, 0) == 0:
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        job.close()
                        child.wait(timeout=5)
                    break
                try:
                    child.wait(timeout=.25)
                except subprocess.TimeoutExpired:
                    pass
    finally:
        job.close()
        server.shutdown()
        server.server_close()


if __name__ == '__main__':
    try:
        main()
    except Exception:
        root = Path(__file__).resolve().parents[1]
        (root / 'logs').mkdir(exist_ok=True)
        (root / 'logs/bootstrap-error.log').write_text(traceback.format_exc(), encoding='utf-8')
        raise
