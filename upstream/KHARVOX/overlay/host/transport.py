"""DOOM startup authority and fresh renderer telemetry; never writes mod settings."""
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import secrets
import threading
import time
import copy

DEFAULT_FOVEATION = '3;0;1;2000;2000;2000;2000;1000;0;7500;11;11;10000;0;1;1;2000;2000;2000;2000;1000;0'
BASE = ['quality', 'scaleBps', 'neuralModel', 'neuralPercent', 'neuralBefore']
EXTRA = ['style', 'intensity', 'structure', 'tone', 'skinAuto', 'skin', 'autoMask', 'detail', 'color', 'sharpness', 'sharpen', 'foveationMode', 'targetMicros', 'overloadMs', 'recoveryMs']
DEFAULTS = dict(zip(BASE + EXTRA, [0, 6667, 0, 85, 0, 2, 120, 127, 134, 1, 100, 0, 100, 100, 0, 0, 0, 11100, 1000, 1000]))
AVAILABLE_MODES = [0, 1, 2, 3]
NEURAL_UNAVAILABLE = 'Neural is temporarily disabled while visual artifacts are investigated. Use DLSS or DLAA.'


def atomic_text(path, text):
    temporary = path.with_name(path.name + '.' + secrets.token_hex(6) + '.tmp')
    try:
        with temporary.open('w', encoding='utf-8', newline='\n') as stream:
            stream.write(text); stream.flush(); os.fsync(stream.fileno())
        # Windows readers can briefly hold a destination without delete sharing.
        # Retry the same atomic rename; never rewrite a partly visible command.
        for attempt in range(25):
            try:
                os.replace(temporary, path)
                break
            except PermissionError:
                if attempt == 24:
                    raise
                time.sleep(.01)
    finally:
        temporary.unlink(missing_ok=True)


def integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError('Setting outside its supported range')
    return value


def foveation(text):
    if not isinstance(text, str) or len(text) > 1024:
        raise ValueError('Invalid foveation configuration')
    parts = text.split(';')
    if len(parts) != 22 or any(not p.isdecimal() for p in parts):
        raise ValueError('Expected common foveation schema 3')
    v = list(map(int, parts))
    ranges = [(3, 3), (0, 1), (0, 1)] + [(0, 4500)] * 4 + [(0, 5000), (0, 1), (2500, 10000), (0, 13), (0, 13), (10000, 20000), (0, 1), (0, 1), (0, 1)] + [(0, 4500)] * 4 + [(0, 5000), (0, 2)]
    for n, (low, high) in zip(v, ranges):
        integer(n, low, high)
    if v[10] not in (0, 10, 11, 12, 13) or v[11] not in (0, 10, 11, 12, 13):
        raise ValueError('Unsupported foveation preset')
    return ';'.join(map(str, v))


def validate(v):
    integer(v['mode'], 0, 4)
    if type(v['preset']) is not int or v['preset'] not in (0, 10, 11, 12, 13):
        raise ValueError('Unsupported SR preset')
    if type(v['outputScale']) not in (float, int) or not math.isfinite(v['outputScale']) or not .5 <= v['outputScale'] <= 3:
        raise ValueError('Invalid output scale')
    ranges = [(0, 2), (3333, 9900), (0, 2), (50, 100), (0, 1), (0, 2), (0, 200), (0, 200), (0, 200), (0, 1), (0, 200), (0, 1), (0, 200), (0, 100), (0, 100), (0, 100), (0, 2), (5000, 50000), (100, 10000), (100, 10000)]
    for key, (low, high) in zip(BASE + EXTRA, ranges):
        integer(v[key], low, high)
    for key in ('inner', 'outer'):
        if not isinstance(v[key], list) or len(v[key]) != 4:
            raise ValueError('Invalid dynamic bounds')
        for n in v[key]:
            if integer(n, 0, 4500) % 100:
                raise ValueError('Dynamic bounds use percentage points')
    if any(a < b for a, b in zip(v['inner'], v['outer'])):
        raise ValueError('Dynamic bounds are inverted')
    integer(v['sequence'], 1, 2**63-1)
    v['foveation'] = foveation(v['foveation'])
    if (v['foveationMode'] and v['mode'] < 3) or (v['foveationMode'] == 2 and v['mode'] == 4):
        raise ValueError('DLAA excludes foveation; Neural supports Fixed only')
    if int(v['foveation'].split(';')[1]) != int(v['foveationMode'] != 0):
        raise ValueError('Foveation mode and configuration disagree')
    return v


def profile(v, mode=None):
    mode = v['foveationMode'] if mode is None else mode
    config = v['foveation'].split(';'); config[1] = str(int(mode != 0))
    return dict(config=';'.join(config), settings=dict(version=1, mode=mode, centralDlaa=False, inner=v['inner'][:], outer=v['outer'][:], targetMicros=v['targetMicros'], sustainedOverloadMs=v['overloadMs'], recoveryDelayMs=v['recoveryMs']))


class LayerTransport:
    def __init__(self, root, pid):
        self.root, self.pid = Path(root), pid
        self.session = f'{pid}-{secrets.token_hex(12)}'
        self.lock = threading.RLock()
        self.clock = ctypes.WinDLL('kernel32').GetTickCount64
        self.clock.restype = ctypes.c_ulonglong
        self.observed = None; self.results = {}; self.initial = self.selection(); self.current = copy.deepcopy(self.initial); self.pending = None; self.sequence = self.initial["sequence"]
        self.profileCache = None
        self.last_trace = None

    def selection(self):
        text = (self.root / 'startup.cfg').read_text(encoding='utf-8'); words = text.split()
        if not words or (words[0], len(words)) not in (('1', 4), ('2', 10), ('3', 34)):
            raise ValueError('Invalid DOOM startup configuration')
        v = dict(DEFAULTS, mode=int(words[1]), preset=int(words[2]), outputScale=float(words[3]), foveation=DEFAULT_FOVEATION, inner=[3000]*4, outer=[1000]*4, sequence=1)
        if (words[0] == '1' and v['mode'] > 2) or (words[0] != '3' and v['mode'] < 1):
            raise ValueError('Legacy startup supports TAA/DLAA only')
        if words[0] != '1':
            v.update(zip(BASE, map(int, words[4:9]))); v['foveation'] = words[9]; v['foveationMode'] = int(words[9].split(';')[1])
        if words[0] == '3':
            v.update(zip(EXTRA, map(int, words[10:25]))); v.update(inner=list(map(int, words[25:29])), outer=list(map(int, words[29:33])), sequence=int(words[33]))
        validate(v)
        return dict(v, revision=hashlib.sha256(text.encode()).hexdigest())

    def persist(self, v, path=None):
        validate(v)
        if v['mode'] not in AVAILABLE_MODES:
            raise ValueError(NEURAL_UNAVAILABLE)
        words = [3, v['mode'], v['preset'], f'{v["outputScale"]:.6f}'] + [v[k] for k in BASE] + [v['foveation']] + [v[k] for k in EXTRA] + v['inner'] + v['outer'] + [v['sequence']]
        atomic_text(path or self.root/'startup.cfg', ' '.join(map(str, words)) + '\n')

    def apply(self, value, save=False, request_id=None, profiles=None):
        validate(value)
        if value['mode'] not in AVAILABLE_MODES:
            raise ValueError(NEURAL_UNAVAILABLE)
        if self.pending:
            raise ValueError('Previous request is still awaiting native confirmation')
        if not self.telemetry():
            raise ValueError('Waiting for fresh renderer telemetry; no request sent')
        self.sequence += 1
        value = copy.deepcopy(value); value['sequence'] = self.sequence
        value['revision'] = str(self.sequence)
        self.pending = dict(value=value, save=save, request_id=request_id, profiles=profiles)
        if request_id:
            self.results[request_id] = dict(controlVersion=2, sessionId=self.session, requestId=request_id, status='queued', persistence='notRequested', revision=str(value['sequence']), code='pending')
        self.persist(value, self.root/f'request-{self.pid}.cfg')
        deadline = time.monotonic()+8
        while time.monotonic() < deadline:
            result = self.reconcile()
            if result is not None:
                if not result['accepted']:
                    raise ValueError(result.get('error') or 'Renderer rejected the settings')
                return
            time.sleep(.05)
        raise ValueError('Awaiting native confirmation; request will not be repeated')

    def reconcile(self):
        if not self.pending:
            return None
        try:
            result = json.loads((self.root/f'ack-{self.pid}.json').read_bytes())
        except (OSError, ValueError):
            return None
        if result.get('revision') != self.pending['value']['sequence']:
            return None
        if result.get('accepted'):
            self.current = self.pending['value']
            if self.pending['profiles'] is not None:
                self.profileCache = copy.deepcopy(self.pending['profiles'])
            if self.pending['save']:
                self.persist(self.current)
                if self.pending['profiles'] is not None:
                    atomic_text(self.root/'foveation-profiles.json', json.dumps(self.pending['profiles']))
        rid = self.pending['request_id']
        if rid:
            self.results[rid] = dict(controlVersion=2, sessionId=self.session, requestId=rid,
                status='accepted' if result.get('accepted') else 'rejected',
                persistence='saved' if result.get('accepted') and self.pending['save'] else 'notRequested',
                revision=str(self.current['sequence']), code=result.get('error') or 'applied')
        self.pending = None
        return result

    def telemetry(self):
        try:
            v = json.loads((self.root/f'status-{self.pid}.json').read_bytes()); age = self.clock() - v['tick']
            if v['pid'] != self.pid or not 0 <= age <= 2000:
                return None
            self.trace_performance(v)
            return v
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def trace_performance(self, live):
        # Preserve measurements across mode changes. gpuMs is an interval
        # between GPU frame markers, INCLUDING waits; not pure GPU work time.
        stamp = (live['tick'], live.get('startupRevision'))
        with self.lock:
            if self.last_trace and stamp[1] == self.last_trace[1] and stamp[0]-self.last_trace[0] < 1000:
                return
            try:
                folder = self.root/'logs'; folder.mkdir(exist_ok=True)
                path = folder/f'performance-{self.pid}.jsonl'
                if path.exists() and path.stat().st_size >= 1024*1024:
                    os.replace(path, folder/f'performance-{self.pid}.previous.jsonl')
                fields = ['tick','startupRevision','requestedMode','mode','preset','width','height','inputWidth','inputHeight','fps','processingMs','framePeriodMs','completed','foveated','evaluations','resets','error']
                sample = {key: live.get(key) for key in fields}
                sample['gpuFrameIntervalIncludingWaitsMs'] = live.get('gpuMs')
                with path.open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps(sample, separators=(',', ':'))+'\n')
                self.last_trace = stamp
            except OSError:
                pass  # Optional evidence must never block Apply or rendering.

    def observe_resolution(self, observed):
        views = observed.get('views', []) if observed.get('available') else []
        if views and all((v['width'], v['height']) == (views[0]['width'], views[0]['height']) for v in views):
            with self.lock:
                self.observed = (views[0]['width'], views[0]['height'])

    def state(self):
        self.reconcile()
        selected = self.current; saved = self.selection()
        matches = all(selected[k] == saved[k] for k in selected if k not in ('revision', 'sequence'))
        live = self.telemetry()
        age = 0 if live else 10000
        names = ['NONE', 'TAA', 'DLAA', 'DLSS', 'DLSS5 / Neural']
        status = names[live.get('mode', 1)] if live else 'Waiting for renderer'
        if live and live.get('error'):
            status = 'ERROR: Processing stopped. Apply retries with the selected settings. '+live['error']
        details = '' if not live else (f"Engine allocation: {live.get('width',0)} x {live.get('height',0)} per eye. "
            f"Input: {live.get('inputWidth',0)} x {live.get('inputHeight',0)}. "
            f"SR/DLAA: {live.get('evaluations',0)} evaluations; Neural: {live.get('neuralEvaluations',0)}; resets: {live.get('resets',0)}. "
            f"Render FPS: {live.get('fps',0):.1f}; processing: {live.get('processingMs',0):.2f} ms.")
        values = dict(Controls=self.controls(selected), Status=status, Label='0', LabelStatus='',
                      Saved='Startup settings saved.' if matches else 'Applied settings are not saved.', Details=details)
        fields = {key:dict(value=value, ageMs=age) for key,value in values.items()}
        return dict(session=self.session, fields=fields, selected=selected, startup=saved, native=live, pending=bool(self.pending), saved=matches, restartRequired=False, capabilities=dict(modes=AVAILABLE_MODES[:], presets=[0, 10, 11, 12, 13], sr=True, foveated=True, neural=False, neuralReason=NEURAL_UNAVAILABLE, startupOnly=False))

    @staticmethod
    def controls(v):
        keys = ['mode','quality','preset','sharpen','scaleBps','neuralPercent','neuralBefore']+EXTRA[:10]+['neuralModel']
        return ';'.join(map(str,[6]+[v[k] for k in keys]))

    def common_command(self, request):
        self.reconcile()
        if request.get('session') != self.session or request.get('expected') != self.controls(self.current):
            raise ValueError('Settings changed; discard or review draft')
        words = request.get('data','').split(';')
        if len(words)!=19 or words[0]!='6' or any(not word.isdecimal() for word in words):
            raise ValueError('Expected rendering control protocol 6')
        keys = ['mode','quality','preset','sharpen','scaleBps','neuralPercent','neuralBefore']+EXTRA[:10]+['neuralModel']
        value = dict(self.current, **dict(zip(keys, map(int,words[1:]))))
        validate(value)
        if request.get('event') == 'Apply':
            self.apply(value)
        elif request.get('event') == 'Save':
            if self.pending or request['data'] != self.controls(self.current):
                raise ValueError('Apply settings before saving')
            self.persist(self.current)
        else:
            raise ValueError('Unsupported rendering action')
        return dict(submitted=True)

    def profiles(self, selected):
        defaults = [profile(dict(DEFAULTS, foveation=DEFAULT_FOVEATION, inner=[3000]*4, outer=[1000]*4), mode) for mode in range(3)]
        try:
            values = copy.deepcopy(self.profileCache) if self.profileCache is not None else json.loads((self.root/'foveation-profiles.json').read_text())
            if not isinstance(values, list) or len(values) != 3:
                raise ValueError('Invalid saved profiles')
            for mode, p in enumerate(values):
                self.profile_selection(selected, p, mode, check_mode=False)
        except (OSError, ValueError, KeyError, TypeError):
            values = json.loads(json.dumps(defaults))
        values[selected['foveationMode']] = profile(selected)
        return values, defaults

    def profile_selection(self, selected, p, mode, check_mode=True):
        s = p['settings']
        if s['version'] != 1 or s['mode'] != mode or s['centralDlaa'] is not False:
            raise ValueError('Invalid common experimental settings')
        v = dict(selected, foveation=foveation(p['config']), foveationMode=mode, inner=s['inner'], outer=s['outer'], targetMicros=s['targetMicros'], overloadMs=s['sustainedOverloadMs'], recoveryMs=s['recoveryDelayMs'])
        validate(v if check_mode else dict(v, mode=3))
        return v

    def foveated_state(self, request_id=None):
        self.reconcile()
        selected = self.current; live = self.telemetry() or {}; profiles, defaults = self.profiles(selected); revision = str(selected['sequence'])
        active = bool(live.get('completed') and live.get('foveated') and live.get('startupRevision') == selected['sequence'] and selected['foveationMode'])
        tel = dict(active=active, status=live.get('dynamicStatus', ''), cuts=live.get('cuts', []), gpuMs=live.get('gpuMs', 0), changeCpuMs=live.get('changeCpuMs', 0), framePeriodMs=live.get('framePeriodMs', 0), targetMs=live.get('targetMs', selected['targetMicros']/1000))
        maps = live.get('maps', [])
        if live.get('width') and len(maps) == 2 and all(len(m) == 9 and m[8] != 0 for m in maps):
            # Native geometry maps output pixels to the binocular plane; the
            # shared preview maps normalized eye coordinates to that plane.
            width, height = live['width'], live['height']
            normalized = [[value * (width if i % 3 == 0 else height if i % 3 == 1 else 1)
                           for i, value in enumerate(m)] for m in maps]
            tel['projection'] = dict(width=width, height=height, maps=normalized)
        descriptor = dict(controlVersion=2, sourceContract=4, configWriteVersion=3, configReadVersions=[1, 2, 3], backend='Vulkan', placement='fixed', geometryMode='binocularFourSides', sessionId=self.session, available=True, recoveryAvailable=True, hostId='DOOM2016', hostVersion='2', operations=['apply', 'applySave'], capabilities=['fixedFoveation', 'fourSideRegions', 'binocularProjection', 'transition', 'centralSupersampling', 'indicator', 'neuralRegions', 'rectangle', 'ellipse', 'spatialPeriphery', 'temporalPeriphery'], generalMode=selected['mode'], neuralActive=selected['mode'] == 4, experimentalVersion=1, dynamicWithNeural=False, experimentalCapabilities=['dynamic'], startupOnly=False)
        snapshot = dict(sessionId=self.session, revision=revision, generalRevision=revision, config=selected['foveation'], runtime=dict(active=active, requestedRevision=revision, appliedRevision=revision if active else '0', message='Activity confirmed by renderer telemetry'), experimental=dict(version=1, selectedMode=selected['foveationMode'], profiles=profiles, presets=defaults, telemetry=tel))
        return dict(descriptor=descriptor, snapshot=snapshot, result=self.results.get(request_id))

    def output_state(self):
        self.reconcile()
        selected = self.current; live = self.telemetry() or {}
        native = live.get('output', {})
        width, height = native.get('width', 0), native.get('height', 0)
        writable = bool(native.get('writable') and native.get('recommendedWidth') and width and height)
        if not width and self.observed: width, height = self.observed
        saved = self.selection()['outputScale'] == selected['outputScale']
        submitted = dict(available=bool(self.observed), views=[dict(width=self.observed[0], height=self.observed[1])]*2) if self.observed else dict(available=False, reason='Waiting for an OpenXR frame')
        # These are measured render extents, not the draft ratio or two copies
        # of the XR output. Never present stale/fallback dimensions as active SR.
        reconstruction = dict(available=False, reason=live.get('error') or 'Waiting for active renderer dimensions')
        if (live.get('completed') and not live.get('error') and live.get('mode') == selected['mode']
                and live.get('mode') in (2, 3, 4) and live.get('startupRevision') == selected['sequence']
                and all(type(live.get(k)) is int and live[k] > 0 for k in ('inputWidth', 'inputHeight', 'width', 'height'))):
            reconstruction = dict(available=True, mode={2:'DLAA', 3:'DLSS', 4:'Neural'}[live['mode']],
                                  inputWidth=live['inputWidth'], inputHeight=live['inputHeight'],
                                  outputWidth=live['width'], outputHeight=live['height'])
        return dict(protocol=1, session=self.session, available=bool(width), canWrite=writable, canSave=writable,
                    authority='OpenXR', width=width, height=height,
                    minWidth=native.get('minWidth') or width, maxWidth=native.get('maxWidth') or width, step=50,
                    expected=f'{width}x{height}:{selected["outputScale"]:.6f}', saved=saved, restartRequired=False,
                    submittedViews=submitted, reconstruction=reconstruction, reason='' if writable else 'Waiting for the native output adapter')

    def exchange(self, request):
        with self.lock:
            op = request.get('op')
            if op == 'state': return self.state()
            if op == 'output_state': return self.output_state()
            if op == 'foveated_state': return self.foveated_state(request.get('requestId'))
            if op == 'command': return self.common_command(request)
            self.reconcile()
            current = self.current
            if op in ('configure', 'output_apply'):
                expected = self.output_state()['expected'] if op == 'output_apply' else current['revision']
                if request.get('session') != self.session or request.get('expected') != expected:
                    raise ValueError('Settings changed; reload the draft')
                v = dict(current)
                if op == 'configure':
                    for key in ['mode', 'preset'] + BASE + EXTRA[:11]:
                        if key in request: v[key] = request[key]
                    desired = 0 if v['mode'] < 3 else 1 if v['mode'] == 4 and v['foveationMode'] == 2 else v['foveationMode']
                    if desired != v['foveationMode']:
                        profiles, _ = self.profiles(current); v = self.profile_selection(v, profiles[desired], desired)
                else:
                    output = self.output_state()
                    if not output['canWrite']: raise ValueError('Live output resize is not available; no settings changed')
                    width = integer(request.get('width'), output['minWidth'], output['maxWidth'])
                    if width == output['width']:
                        if request.get('save') is True: self.persist(current)
                        return dict(submitted=True, selection=self.output_state())
                    native = (self.telemetry() or {}).get('output', {})
                    if not native.get('recommendedWidth'): raise ValueError('Output authority is no longer available')
                    v['outputScale'] = width/native['recommendedWidth']
                self.apply(v, save=request.get('save') is True)
                return dict(submitted=True, selection=self.output_state()) if op == 'output_apply' else dict(saved=self.state()['saved'], effective=False, applied=True, restartRequired=False, state=self.state())
            if op == 'foveated_command':
                rid = request.get('requestId')
                if not isinstance(rid, str) or not 1 <= len(rid) <= 128 or request.get('sessionId') != self.session or request.get('controlVersion') != 2:
                    raise ValueError('Invalid control session')
                if rid in self.results: return self.foveated_state(rid)
                if request.get('event') not in ('DLSS_FOVEADO.Apply','DLSS_FOVEADO.ApplySave'): raise ValueError('Unsupported foveation action')
                packet = request.get('packet', '').split('|')
                if len(packet) != 3 or packet[:2] != [str(current['sequence'])]*2: raise ValueError('Settings changed; reload the foveation draft')
                settings = request['experimental']; mode = integer(settings['mode'], 0, 2)
                v = self.profile_selection(current, dict(config=packet[2], settings=settings), mode)
                profiles, _ = self.profiles(current); profiles[mode] = profile(v)
                save = request['event'] == 'DLSS_FOVEADO.ApplySave'
                self.apply(v, save=save, request_id=rid, profiles=profiles)
                v = self.current
                if len(self.results) > 128: del self.results[next(iter(self.results))]
                return self.foveated_state(rid)
            raise ValueError('Unsupported DOOM Layer operation')
