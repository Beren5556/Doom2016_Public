"""Version 1 shared BGRA surface and bounded pointer-event ring. No VR startup."""
import ctypes as C
from ctypes import wintypes as W
import io
import mmap
import struct
import secrets
from PIL import Image

WIDTH,HEIGHT=960,900
PANEL_WIDTH,PANEL_HEIGHT=672,720
class Surface:
    def __init__(self,pid):
        self.kernel=C.WinDLL('kernel32',use_last_error=True)
        self.kernel.CreateMutexW.argtypes=[C.c_void_p,W.BOOL,W.LPCWSTR]
        self.kernel.CreateMutexW.restype=W.HANDLE
        self.kernel.WaitForSingleObject.argtypes=[W.HANDLE,W.DWORD]
        self.kernel.ReleaseMutex.argtypes=[W.HANDLE]
        self.kernel.GetTickCount64.restype=C.c_ulonglong
        self.kernel.OpenProcess.argtypes=[W.DWORD,W.BOOL,W.DWORD]
        self.kernel.OpenProcess.restype=W.HANDLE
        name=rf'Local\ExternalOverlay.Surface.{pid}'
        self.owner=self.kernel.CreateMutexW(None,True,name+'.Host')
        if not self.owner or C.get_last_error()==183: raise RuntimeError('An overlay host already owns this game session')
        self.process=self.kernel.OpenProcess(0x100000,False,pid)
        if not self.process: raise OSError('Target game process unavailable')
        self.mutex=self.kernel.CreateMutexW(None,False,name+'.Mutex')
        if not self.mutex: raise OSError('Cannot create surface mutex')
        self.map=mmap.mmap(-1,4096+WIDTH*HEIGHT*4,tagname=name)
        self.lock()
        try:
            self.map[:4096]=bytes(4096)
            struct.pack_into('<I',self.map,0,1)
            struct.pack_into('<I',self.map,36,secrets.randbits(32) or 1)
        finally: self.unlock()
    def lock(self):
        if self.kernel.WaitForSingleObject(self.mutex,1000) not in (0,128): raise TimeoutError('Surface mutex timeout')
    def unlock(self): self.kernel.ReleaseMutex(self.mutex)
    def close_overlay(self):
        self.lock()
        try:
            seq=struct.unpack_from('<I',self.map,40)[0]
            struct.pack_into('<I',self.map,40,(seq+1)&0xffffffff)
        finally:self.unlock()
        return dict(closeRequested=True)
    def information(self):
        self.lock()
        try:
            data=bytes(self.map[3136:3136+832])
        finally:self.unlock()
        version,active,vendor,layers,backend,inputs,tick,runtime_version=struct.unpack_from('<6I2Q',data)
        age=self.kernel.GetTickCount64()-tick
        if version!=1 or not tick or age<0 or age>3000:
            return dict(available=False,reason='Waiting for an active OpenXR session')
        def text(start,size):return data[start:start+size].split(b'\0',1)[0].decode('utf-8',errors='replace')
        return dict(available=bool(active),system=text(40,256),runtime=text(296,128),
                    runtimeVersion=f'{runtime_version>>48}.{(runtime_version>>32)&65535}.{runtime_version&0xffffffff}',
                    leftProfile=text(424,192),rightProfile=text(616,192),vendor=vendor,
                    graphics=f'D3D{backend}' if backend in (11,12) else 'Vulkan' if backend==100 else 'Unknown',maxLayers=layers,
                    inputMask=inputs,bindings=list(struct.unpack_from('<6i',data,808)),ageMs=age,
                    reason='' if active else 'OpenXR session ended')
    def resolution(self):
        self.lock()
        try:data=bytes(self.map[3968:4096])
        finally:self.unlock()
        version,count,tick,view_type,flags,max_width,max_height=struct.unpack_from('<2IQ4I',data)
        age=self.kernel.GetTickCount64()-tick
        if version!=1 or not tick or age<0 or age>3000 or not flags&1:
            return dict(available=False,reason='Waiting for an OpenXR frame',views=[])
        views=[]
        for i in range(min(count,4)):
            values=struct.unpack_from('<6I',data,32+i*24)
            views.append(dict(zip(['width','height','recommendedWidth','recommendedHeight','maxWidth','maxHeight'],values)))
        valid=bool(flags&2) and bool(views) and all(v['width'] and v['height'] for v in views)
        return dict(available=valid,reason='' if valid else 'No projection views submitted',views=views,
                    viewCount=count,viewType=view_type,ambiguous=bool(flags&4),truncated=bool(flags&8),
                    systemMaxWidth=max_width,systemMaxHeight=max_height,ageMs=age)
    def frame(self,png):
        image=Image.open(io.BytesIO(png))
        if image.size!=(PANEL_WIDTH,PANEL_HEIGHT): raise ValueError('Panel must be 672x720')
        panel=image.convert('RGBA').tobytes('raw','BGRA')
        padding=bytes((WIDTH-PANEL_WIDTH)*4)
        pixels=b''.join(panel[y*PANEL_WIDTH*4:(y+1)*PANEL_WIDTH*4]+padding for y in range(PANEL_HEIGHT))+bytes(WIDTH*(HEIGHT-PANEL_HEIGHT)*4)
        self.lock()
        try:
            self.map[4096:]=pixels
            seq=struct.unpack_from('<I',self.map,4)[0]
            struct.pack_into('<I',self.map,4,(seq+1)&0xffffffff)
            struct.pack_into('<Q',self.map,24,self.kernel.GetTickCount64())
        finally: self.unlock()
    def heartbeat(self):
        self.lock()
        try:struct.pack_into('<Q',self.map,24,self.kernel.GetTickCount64())
        finally:self.unlock()
        return dict(ok=True)
    def input(self):
        self.lock()
        try:
            _,frame,visible,write,read,status=struct.unpack_from('<6I',self.map)
            if status==2:
                # Cancel the whole ambiguous batch, never replay partial clicks.
                # Close once and rearm the transport; keep the host alive so the
                # user can open it again. This also handles a saturated old layer.
                struct.pack_into('<I',self.map,16,write)
                struct.pack_into('<I',self.map,20,0)
                struct.pack_into('<I',self.map,8,0)
                close=struct.unpack_from('<I',self.map,40)[0]
                struct.pack_into('<I',self.map,40,(close+1)&0xffffffff)
                return dict(visible=False,frame=frame,status=0,reset=True,events=[dict(type=4,x=0,y=0,value=0)],shutdown=self.kernel.WaitForSingleObject(self.process,0)==0)
            if write-read>128: raise RuntimeError('Input ring overrun')
            events=[]
            for seq in range(read+1,write+1):
                record=struct.unpack_from('<IIffiI',self.map,64+((seq-1)%128)*24)
                if record[0]!=seq: raise RuntimeError('Input sequence mismatch')
                events.append(dict(type=record[1],x=record[2],y=record[3],value=record[4]))
            struct.pack_into('<I',self.map,16,write)
            flags,coalesced,overflows,sync_result,sync_count=struct.unpack_from('<3IiI',self.map,44)
            return dict(visible=bool(visible),frame=frame,status=status,events=events,diagnostics=dict(flags=flags,coalesced=coalesced,overflows=overflows,syncResult=sync_result,syncCount=sync_count),shutdown=self.kernel.WaitForSingleObject(self.process,0)==0)
        finally: self.unlock()
