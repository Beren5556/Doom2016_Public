# Build and package engine 1.3.0

Configure rejects mismatched versions in CMake, provenance.json and the public
EngineVersion header. The installed consumer also checks the CMake package
version against the installed header. Regression fixtures are available through
scripts/Test-EngineVersion.ps1; the source package must include
cmake/CheckEngineVersion.cmake when copied into a host.

Run the packaging commands from the private source checkout's root in PowerShell 7
with Git and Visual Studio 2022 C++ x64 tools (the installed ZIP does not contain build scripts):

```powershell
./scripts/Package-Engine.ps1 -Mode Core
./scripts/Package-Engine.ps1 -Mode D3D11
./scripts/Package-Engine.ps1 -Mode NGX
```

The script discovers CMake on PATH or through Visual Studio's vswhere. An explicit
`-CMake 'C:/path/to/cmake.exe'` is also supported. NGX defaults to the SDK input
inside this project; use `-NgxSdk` for another explicitly selected SDK directory.
No source, SDK or installed-profile path in BL3 is used. It creates a new run
directory and does not delete earlier builds/evidence.

Each run first exports the immutable 0.3.0 baseline commit with Git and verifies
all 21 original receipt entries against that snapshot. It then inventories the
current sources separately; changed 1.3.0 sources are not claimed to match 0.3.0.
Keep the baseline commit available in the checkout; a shallow clone without it
cannot run this historical verification. Original evidence is never overwritten.

Each run configures/builds Release, executes CTest (Core: 4, D3D11/NGX: 7), installs
to a staging prefix, adds documentation and a content manifest, produces a ZIP,
extracts it elsewhere and checks every SHA256. It then builds/tests a copied
`examples/consumer` against the extracted installed package. For NGX it also
copies the SDK link library to an independent consumer input directory, so the
export resolves that supplied SDK instead of a baked-in development path.

Artifacts are under `out/packages/<run>/`; logs and the validation receipt are
under `evidence/package-validation/<run>/`. This is a repeatable build procedure,
not a claim of bit-identical compiler output across machines or toolchains.
Receipt hashes identify the exact output checked in that run.

The ZIP contains a development package: static libraries, public headers,
CMake package files, an independent example consumer, this guide, the host
integration guide, compatibility/dependency notes and (for NGX)
the original NVIDIA license notice. It contains no runtime DLL, game hook,
UEVR profile, Neural provider/model or Layer UI. Extract to a chosen SDK prefix.

```powershell
cmake -S examples/consumer -B build/consumer -A x64 `
  -DCMAKE_PREFIX_PATH=C:/sdk/FoveatedDLSS `
  -DDLSS_FOVEADO_NGX_SDK=C:/sdk/NVIDIA-DLSS-310.9.1
cmake --build build/consumer --config Release
ctest --test-dir build/consumer -C Release --output-on-failure
```

Omit the NGX argument for Core and D3D11 packages. Consumers use
`find_package(DlssFoveado 1.3 CONFIG REQUIRED)` and choose `dlss_foveado::core`,
`dlss_foveado::d3d11` or `dlss_foveado::ngx` according to the packaged mode.
The consumer exercises schema 3, synthetic overlapping projections and extension
1 through the installed export. D3D11 packages also construct the shader backend
on WARP; NGX packages link the NGX factory and check its invalid-device rejection
before runtime initialization. GPU/pipeline behavior is covered by engine CTest.
No real NVIDIA inference is executed.
