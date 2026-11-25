"""Real independent host/UI contract test, with no UEVR or game process."""
from pathlib import Path
import importlib.util,os,subprocess,threading,urllib.request,urllib.error
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('demo',ROOT/'examples/demo_host.py');demo=importlib.util.module_from_spec(spec);spec.loader.exec_module(demo)
adapter=demo.DemoAdapter();server,url=demo.make_server(adapter,ui_root=ROOT/'examples/web')
threading.Thread(target=server.serve_forever,daemon=True).start()
try:
    try:urllib.request.urlopen(urllib.request.Request(url.split('/?')[0]+'/output/state',data=b'',headers={'X-Overlay-Token':'wrong'}));raise AssertionError('Missing authentication')
    except urllib.error.HTTPError as e:assert e.code==403
    subprocess.run([os.environ.get('NODE_EXE','node'),str(ROOT/'tests/demo_ui.cjs'),url],check=True,timeout=25)
    assert adapter.width==3053
    print('PASS independent extracted host, shared widgets, demo authority and authentication')
finally:server.shutdown();server.server_close()
