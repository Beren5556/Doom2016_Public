# ExternalOverlayXR — 0.1.1

**Otro Codex / otro PC: [empezar aquí](docs/HANDOFF_OTHER_PC.md).** Guía autónoma de acceso, dependencias, compilación y pruebas; no requiere el chat original.

Private reusable Windows/OpenXR D3D11/D3D12 overlay module, extracted from
the accepted AC7 0.8.3-r2 implementation. This repository is the development
base for future UEVR mods, independent VR mods and native PCVR integrations.

Version 0.1.1 contains only the shared overlay, host and widgets. It has no
frame-generation provider, loader, settings or dependency. The release audit
checks the operational source and delivery files, including the final DLL.
See [changes](CHANGELOG.md) and [consumer migration](docs/MIGRATION_0.1.1.md).

Carlos accepted the 0.1.1 close on 2026-10-07. Sources and the integration kit
are released exclusively in this private repository: [v0.1.1](https://github.com/Beren5556/ExternalOverlayXR/releases/tag/v0.1.1).
See the [current delivery report](ENTREGA_0.1.1_r2.md),
[release identities](docs/validation/publication-0.1.1.json) and
[consumer compatibility record](docs/COMPATIBILITY.md).

**Start with [the complete documentation index](docs/INDEX.md).**

Consumer packaging must deploy the layer manifest and DLL before its activation
bootstrap runs. See [first installation and bootstrap order](docs/FIRST_INSTALL_AND_BOOTSTRAP.md)
for the empty-profile check, late-extraction failure and AC7 reference evidence.

The module consists of the native layer, an external host and shared panel
widgets. Consumers supply startup/lifetime integration, parameter authority,
an output-resolution adapter and a game-specific view. Controllers, headset
information and submitted resolution telemetry stay in the common layer.

Build native layer/tests with `cmake -S . -B build -A x64`, then
`cmake --build build --config Release` and `ctest --test-dir build -C Release`.
The host needs Python/Pillow, Node/Playwright and installed Microsoft Edge;
see the pinned dependency files and host guide. No game is started by tests.

Run `examples/demo_host.py` for an independent in-memory adapter and panel.
It demonstrates contracts only; it does not change a game or certify VR.

The initial native source identity is recorded in provenance.json. Its
accepted AC7 binary hash is 4351d18574e1eac52dc352d45296c311d936a19c78b6b628824ab1bdf724ecb7.
That is historical evidence for 0.1.0's origin, not the 0.1.1 DLL. New release
hashes and executed checks are recorded in docs/validation/release-0.1.1.json;
the final release kit identity is in docs/validation/publication-0.1.1.json.
AC7's UEVR internal resolution accessor is deliberately absent. Public
downloads are managed separately; this project and its source remain private.
