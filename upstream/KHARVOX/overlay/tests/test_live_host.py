"""Authenticated real UI plus a deterministic native acknowledgement peer.

GPU switching is tested separately by doom_temporal_bridge_runtime_tests.
This peer verifies persistence and request/ack ordering, not visual rendering.
"""
from pathlib import Path
import copy
import json
import os
import subprocess
import sys
import tempfile
import threading
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'host'))
from transport import LayerTransport, atomic_text
from server import make_server

with tempfile.TemporaryDirectory(prefix='doom-live-host-') as temporary:
    root = Path(temporary)
    config = root/'startup.cfg'
    config.write_text('1 2 11 1.0\n')
    host = LayerTransport(root, os.getpid())
    host.observe_resolution(dict(available=True, views=[dict(width=3200, height=3400)]*2))
    effective = copy.deepcopy(host.initial)
    stop = threading.Event()
    reject_next = threading.Event()
    failures = []

    def peer():
        last = 0
        try:
            while not stop.is_set():
                request = root/f'request-{host.pid}.cfg'
                if request.exists():
                    fields = request.read_text().split()
                    sequence = int(fields[-1])
                    if sequence > last:
                        reject = reject_next.is_set(); reject_next.clear()
                        if not reject:
                            effective.update(mode=int(fields[1]), preset=int(fields[2]), sequence=sequence,
                                             foveationMode=int(fields[21]), outputScale=float(fields[3]), scaleBps=int(fields[5]))
                        atomic_text(root/f'ack-{host.pid}.json', json.dumps(dict(revision=sequence, accepted=not reject, error='Test rejection' if reject else '')))
                        last = sequence
                atomic_text(root/f'status-{host.pid}.json', json.dumps(dict(
                    pid=host.pid, tick=host.clock(), startupRevision=effective['sequence'],
                    mode=effective['mode'], preset=effective['preset'], evaluations=120, resets=2,
                    width=4000, height=4250, completed=True, neuralEvaluations=120 if effective['mode']==4 else 0,
                    inputWidth=round(4000*effective['scaleBps']/10000) if effective['mode']>=3 else 4000,
                    inputHeight=round(4250*effective['scaleBps']/10000) if effective['mode']>=3 else 4250,
                    maps=[[.8/4000,0,offset,0,.87/4250,.065,0,0,1] for offset in (0,.2)],
                    foveated=bool(effective['foveationMode']), output=dict(writable=True,
                        width=round(3200*effective['outputScale']),height=round(3400*effective['outputScale']),
                        recommendedWidth=3200,recommendedHeight=3400,minWidth=1600,maxWidth=5600))))
                stop.wait(.02)
        except Exception as error:
            failures.append(error)

    thread = threading.Thread(target=peer); thread.start()
    server = None
    try:
        assert stop.wait(.1) is False
        def change(mode, save=False):
            state = host.state()
            return host.exchange(dict(op='configure', session=host.session, expected=state['selected']['revision'], mode=mode, save=save))

        original = config.read_bytes()
        for update in ({'mode':True},{'mode':4},{'mode':5},{'preset':9},{'scaleBps':12000},{'neuralModel':3},{'sharpness':-1}):
            try:
                host.exchange(dict(op='configure', session=host.session, expected=host.current['revision'], **update))
                raise AssertionError('Invalid controls accepted')
            except ValueError:
                pass
        assert config.read_bytes()==original
        assert change(3)['applied']
        assert host.current['mode']==3 and config.read_bytes()==original
        assert not host.state()['saved']
        assert change(3, True)['saved'] and host.selection()['mode']==3
        assert host.state()['capabilities']['modes']==[0,1,2,3]
        assert host.state()['capabilities']['neural'] is False
        blocked=dict(host.current,mode=4)
        for event in ['Apply','Save']:
            before_sequence=host.sequence
            try:
                host.common_command(dict(session=host.session,expected=host.controls(host.current),data=host.controls(blocked),event=event))
                raise AssertionError('Neural command accepted')
            except ValueError:
                pass
            assert host.sequence==before_sequence and host.pending is None
        try:
            host.persist(blocked)
            raise AssertionError('Neural startup persisted')
        except ValueError:
            pass
        saved = config.read_bytes()
        reject_next.set()
        try:
            change(2, True)
            raise AssertionError('Native rejection was accepted')
        except ValueError as error:
            assert 'Test rejection' in str(error)
        assert host.current['mode']==3 and config.read_bytes()==saved
        assert host.output_state()['width']==3200  # OpenXR, not 4000 engine allocation.
        stop.wait(.1)
        render = host.output_state()['reconstruction']
        assert render == dict(available=True, mode='DLSS', inputWidth=2667, inputHeight=2833, outputWidth=4000, outputHeight=4250)
        # A rejected processing frame or superseded revision must not keep the
        # last successful dimensions visible as though DLSS were still active.
        real_telemetry = host.telemetry
        captured = real_telemetry()
        try:
            for override in [dict(completed=False, error='RG32F failure'), dict(startupRevision=0), dict(inputWidth=0)]:
                host.telemetry = lambda: dict(captured, **override)
                assert not host.output_state()['reconstruction']['available']
            host.telemetry = lambda: None
            assert not host.output_state()['reconstruction']['available']
        finally:
            host.telemetry = real_telemetry
        try:
            host.exchange(dict(op='output_apply', session=host.session, expected='stale', width=3000, save=True))
            raise AssertionError('Stale output resize was acknowledged')
        except ValueError:
            pass
        change(2, True)
        stop.wait(.1)
        render = host.output_state()['reconstruction']
        assert render['mode']=='DLAA' and render['inputWidth']==render['outputWidth']==4000
        before_output=config.read_bytes()
        output=host.output_state()
        assert host.exchange(dict(op='output_apply', session=host.session, expected=output['expected'], width=3000))['submitted']
        assert host.current['outputScale']==3000/3200 and config.read_bytes()==before_output
        stop.wait(.1)
        output=host.output_state();assert output['width']==3000 and not output['saved']
        revision=host.current['sequence']
        assert host.exchange(dict(op='output_apply', session=host.session, expected=output['expected'], width=3000,save=True))['submitted']
        assert host.current['sequence']==revision and host.selection()['outputScale']==3000/3200
        control = host.controls(host.current)
        revision = host.current['sequence']
        host.exchange(dict(op='command', session=host.session, expected=control, data=control, event='Save'))
        assert host.current['sequence']==revision  # Save never reevaluates/restarts the renderer.
        server, url = make_server(host, ui_root=ROOT/'web')
        threading.Thread(target=server.serve_forever, daemon=True).start()
        try:
            urllib.request.urlopen(urllib.request.Request(url.split('/?')[0]+'/api', data=b'{"op":"state"}', headers={'X-Overlay-Token':'wrong'}))
            raise AssertionError('Unauthenticated write accepted')
        except urllib.error.HTTPError as error:
            assert error.code==403
        subprocess.run([os.environ.get('NODE_EXE','node'), str(ROOT/'tests/ui.cjs'), url, sys.argv[1]], check=True, timeout=60)
        assert host.selection()['mode']==3 and host.current['mode']==3
        samples=[json.loads(line) for line in (root/'logs'/f'performance-{host.pid}.jsonl').read_text().splitlines()]
        assert {2,3} <= {sample['mode'] for sample in samples}
        assert all('gpuFrameIntervalIncludingWaitsMs' in sample and 'processingMs' in sample for sample in samples)
        assert not failures, failures
        print('PASS live Apply, Save, rejection, observed resolution, pointer popup across polls and authenticated UI')
    finally:
        stop.set(); thread.join()
        if server:
            server.shutdown(); server.server_close()
