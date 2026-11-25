# FoveatedDLSS 1.2.0

Adopts the approved exp6 engine and final JSON wait adjustment for B3 VR 0.9.7.
Dynamic uses bounded prepared resources and keeps current per-eye histories
during central-context transitions. The accepted four-side geometry and
binocular projections remain unchanged.

- Opt-in Dynamic with fixed center, minimum/maximum bounds, three prepared
  central levels, continuous transition and real memory-budget/paging guards.
- Both waits default to 1000 ms (100..10000), JSON only; movement speed unchanged.
- Direct regional guides for Neural-after and optional central DLAA with
  peripheral SR. Neural + Dynamic is rejected; Fixed + Neural is retained.
- Engine1.2.0, base source API4/config3/control2, experimental extension1.
  Rebuild consumers/backends and request CMake version1.2. No binary ABI promise.

[Extension and migration](https://github.com/Beren5556/FOVEATED_DLSS/blob/v1.2.0/docs/EXPERIMENTAL_CONTRACT_1.0.md),
[build instructions](https://github.com/Beren5556/FOVEATED_DLSS/blob/v1.2.0/docs/BUILD_AND_PACKAGE.md),
[dependency/license inventory](https://github.com/Beren5556/FOVEATED_DLSS/blob/v1.2.0/docs/DEPENDENCIES.md).

Three Windows x64 Release **development SDKs**, Core/D3D11/NGX, with headers,
static libraries, relocated CMake exports, independent consumer, documentation
and file manifest. SHA256SUMS.txt identifies the exact ZIPs. Complete sources,
tests, scripts, evidence and NVIDIA SDK build input/license remain in this
private repository. NGX runtime and private Neural provider/model are supplied
by the host. These assets are not B3 game profiles or Layer installers.

Validation: Core3/3; D3D11/NGX6/6; independent installed consumer1/1 per package.
CPU/WARP includes148 stereo transition pairs and simulated72/90/120Hz recovery.
NGX is compiled/linked without real inference. Historical exp6 user feedback is
not a new benchmark or universal headset claim. B3 integration and installation
belong to the B3 0.9.7 coordinator; no B3 publication is included here.
