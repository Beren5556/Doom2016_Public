"""Independent in-memory adapter demo. No game, UEVR, VR startup or disk settings."""
from pathlib import Path
import sys,threading
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'host'))
from host import make_server
class DemoAdapter:
    def __init__(self):self.width=3003;self.revision=1;self.lock=threading.Lock()
    def state(self):
        return dict(protocol=1,session='demo-engine',available=True,canWrite=True,canSave=False,authority='Demo engine (in memory only)',width=self.width,height=round(self.width*1.06),minWidth=1000,maxWidth=6000,step=50,expected=str(self.revision),saved=False,reason='Demonstration only; no game output or persistence')
    def exchange(self,request):
        with self.lock:
            if request['op']=='output_state':return self.state()
            if request['op']=='output_apply':
                if request.get('session')!='demo-engine' or request.get('expected')!=str(self.revision):raise ValueError('Selection changed; review draft')
                width=request.get('width')
                if type(width) is not int or not 1000<=width<=6000 or request.get('save') is not False:raise ValueError('Invalid demo request or unsupported save')
                self.width=width;self.revision+=1
                return dict(submitted=True,effective=False,selection=self.state())
            raise ValueError('Unsupported demo operation')
if __name__=='__main__':
    server,url=make_server(DemoAdapter(),ui_root=ROOT/'examples/web')
    print('Local demonstration URL (contains temporary token):',url,flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close()
