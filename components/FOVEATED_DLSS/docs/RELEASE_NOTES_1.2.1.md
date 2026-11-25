# FoveatedDLSS 1.2.1

Corrects the engine identity exposed to host descriptors: the1.2.0 release
mistakenly retained EngineVersion="1.1.0" in its public header. Header, CMake
and provenance now consistently report **1.2.1**. Use this patch for B3 0.9.7.
The published1.2.0 tag and assets remain unchanged for traceability.

Configure now rejects an identity mismatch, and the independent installed
consumer checks the public header against its CMake package version.
Rendering, geometry, projections and runtime behavior are unchanged from the
approved exp6 implementation in1.2.0. Base API4/config3/control2 and opt-in
experimental extension1 remain unchanged, including JSON-only1000ms waits.

Three private Windows x64 Release development SDKs: Core, D3D11 and NGX.
Complete sources, tests, build scripts, NVIDIA SDK/license and evidence are in
this private repository. Runtime NGX and Neural provider/model remain external.
These are not B3 profiles. B3 installation and manual acceptance stay with its
coordinator; no B3 publication is included.

[Extension contract](https://github.com/Beren5556/FOVEATED_DLSS/blob/v1.2.1/docs/EXPERIMENTAL_CONTRACT_1.0.md),
[build and consumption](https://github.com/Beren5556/FOVEATED_DLSS/blob/v1.2.1/docs/BUILD_AND_PACKAGE.md).

Validation: identity mismatch fixtures, Core3/3, D3D11/NGX6/6 and installed
consumer1/1 per package. CPU/WARP only; NGX linked without real inference.
SHA256SUMS identifies the exact verified assets.
