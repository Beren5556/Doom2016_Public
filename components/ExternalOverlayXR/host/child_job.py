"""Own only the external renderer and its browser children, never the game."""
import ctypes as C
from ctypes import wintypes as W

class BasicLimits(C.Structure):
    _fields_=[('processTime',C.c_longlong),('jobTime',C.c_longlong),('flags',W.DWORD),('minWorkingSet',C.c_size_t),('maxWorkingSet',C.c_size_t),('activeProcesses',W.DWORD),('affinity',C.c_size_t),('priority',W.DWORD),('scheduling',W.DWORD)]
class IoCounters(C.Structure):
    _fields_=[(name,C.c_ulonglong) for name in ['readOps','writeOps','otherOps','readBytes','writeBytes','otherBytes']]
class ExtendedLimits(C.Structure):
    _fields_=[('basic',BasicLimits),('io',IoCounters),('processMemory',C.c_size_t),('jobMemory',C.c_size_t),('peakProcessMemory',C.c_size_t),('peakJobMemory',C.c_size_t)]
class ChildJob:
    def __init__(self):
        self.kernel=C.WinDLL('kernel32',use_last_error=True)
        self.kernel.CreateJobObjectW.argtypes=[C.c_void_p,W.LPCWSTR];self.kernel.CreateJobObjectW.restype=W.HANDLE
        self.kernel.SetInformationJobObject.argtypes=[W.HANDLE,C.c_int,C.c_void_p,W.DWORD]
        self.kernel.AssignProcessToJobObject.argtypes=[W.HANDLE,W.HANDLE]
        self.kernel.CloseHandle.argtypes=[W.HANDLE]
        self.handle=self.kernel.CreateJobObjectW(None,None)
        if not self.handle:raise C.WinError(C.get_last_error())
        limits=ExtendedLimits();limits.basic.flags=0x2000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not self.kernel.SetInformationJobObject(self.handle,9,C.byref(limits),C.sizeof(limits)):
            error=C.get_last_error();self.close();raise C.WinError(error)
    def assign(self,process):
        if not self.kernel.AssignProcessToJobObject(self.handle,int(process._handle)):raise C.WinError(C.get_last_error())
    def close(self):
        if self.handle:self.kernel.CloseHandle(self.handle);self.handle=None
