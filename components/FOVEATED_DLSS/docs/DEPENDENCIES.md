# Development inputs and package contents

| Component | Provenance / version | Private source repository | Installed engine ZIP |
|---|---|---|---|
| Engine C++, shaders, CMake, tests | Original implementation, 0.3.0 / contract 3 | Complete sources and receipts | Static libraries, headers, CMake export |
| NVIDIA DLSS SDK input | 310.9.1, hashes in transfer-manifest.json | Imported subset and original LICENSE.txt | Not bundled; NGX consumers supply the SDK path |
| SDK reference nvngx_dlss.dll | Vendor SDK file, SHA256 in transfer manifest | Preserved inside the SDK input directory | Excluded |
| NGX runtime/cache/project identity | Selected by each host | No host runtime/configuration copied | Host supplied |
| Neural provider/model/transport | Private host component | Not part of this engine transfer | Not bundled |
| BL3 adapter/controller/Lua | Accepted r3 historical references | references/bl3, origin evidence | Not bundled |
| Cheeky | Behavioral reference in provenance.json | No imported source | No plugin/binary |

The SDK directory contains a vendor reference DLL. Its presence in development
inputs does not install or select a runtime and does not make it part of the
portable engine package. NGX's explicit SDK link input is
`lib/Windows_x86_64/x64/nvsdk_ngx_d.lib`; the consumer resolves its own path.

The original NVIDIA license is retained without modification at
`dependencies/SDK/NVIDIA-DLSS-310.9.1/LICENSE.txt`. NGX packages also carry a copy
as a third-party notice for the linked SDK material. No public source license is
selected for this private project. This inventory records included files and
their provenance; it does not grant redistribution rights for host dependencies.

Windows builds require CMake >= 3.24, C++20, MSVC x64 and a Windows SDK for D3D11.
Compiler and SDK versions are recorded per validation run. Compiler, target,
architecture, runtime-library selection and build configuration must match the
consumer. Core-only needs no NVIDIA SDK. Build outputs remain local and ignored.

Neural310.9.16 patched runtime is a host-supplied private input, not an engine
DLL or SDK build dependency. The integration kit pins its hash and verifies
per-eye proxy/real-library layout; see NEURAL_INTEGRATION_310.9.16.md.
