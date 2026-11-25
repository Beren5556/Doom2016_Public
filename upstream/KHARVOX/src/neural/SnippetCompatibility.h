#pragma once
// B3 v0.9.11 pinned nr_cost_scaler compatibility shim. Copyright (c) 2026 xen.
// MIT license in SnippetCompatibility.LICENSE. Patches only the borrowed NR module IAT.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
namespace kharvox::neural::compatibility {
inline void Log(const char* message){std::fprintf(stderr,"%s\n",message);}
typedef DWORD (WINAPI *PFN_GetModuleFileNameW)(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
static PFN_GetModuleFileNameW g_origGetModuleFileNameW = nullptr;

static DWORD WINAPI Hooked_GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize) {
    if (g_origGetModuleFileNameW) {
        g_origGetModuleFileNameW(hModule, lpFilename, nSize);
    } else {
        GetModuleFileNameW(hModule, lpFilename, nSize);
    }
    wchar_t sysDir[MAX_PATH] = { 0 };
    if (GetSystemDirectoryW(sysDir, MAX_PATH) > 0) {
        swprintf_s(lpFilename, nSize, L"%ls\\nvngx.dll", sysDir);
    } else {
        wcscpy_s(lpFilename, nSize, L"C:\\Windows\\System32\\nvngx.dll");
    }
    return (DWORD)wcslen(lpFilename);
}

static void HookSnippetCallerCheck(HMODULE targetModule) {
    if (!targetModule) return;

    __try {
        BYTE* base = (BYTE*)targetModule;
        IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)base;
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return;

        IMAGE_NT_HEADERS* ntHeaders = (IMAGE_NT_HEADERS*)(base + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return;

        IMAGE_DATA_DIRECTORY importDir = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.VirtualAddress == 0 || importDir.Size == 0) return;

        IMAGE_IMPORT_DESCRIPTOR* importDesc = (IMAGE_IMPORT_DESCRIPTOR*)(base + importDir.VirtualAddress);

        for (; importDesc->Name != 0; ++importDesc) {
            const char* modName = (const char*)(base + importDesc->Name);
            if (_stricmp(modName, "KERNEL32.dll") == 0) {
                IMAGE_THUNK_DATA* thunkOrig = (IMAGE_THUNK_DATA*)(base + (importDesc->OriginalFirstThunk ? importDesc->OriginalFirstThunk : importDesc->FirstThunk));
                IMAGE_THUNK_DATA* thunk = (IMAGE_THUNK_DATA*)(base + importDesc->FirstThunk);

                for (; thunk->u1.Function != 0; ++thunk, ++thunkOrig) {
                    if (!(thunkOrig->u1.Ordinal & IMAGE_ORDINAL_FLAG)) {
                        IMAGE_IMPORT_BY_NAME* importByName = (IMAGE_IMPORT_BY_NAME*)(base + thunkOrig->u1.AddressOfData);
                        if (strcmp(importByName->Name, "GetModuleFileNameW") == 0) {
                            g_origGetModuleFileNameW = (PFN_GetModuleFileNameW)thunk->u1.Function;
                            DWORD oldProtect = 0;
                            if (VirtualProtect(&thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &oldProtect)) {
                                thunk->u1.Function = (ULONG_PTR)&Hooked_GetModuleFileNameW;
                                VirtualProtect(&thunk->u1.Function, sizeof(void*), oldProtect, &oldProtect);
                                Log("[Proxy] Installed GetModuleFileNameW hook in real module IAT");
                            }
                            return;
                        }
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("[Proxy] SEH caught exception in HookSnippetCallerCheck");
    }
}


}
