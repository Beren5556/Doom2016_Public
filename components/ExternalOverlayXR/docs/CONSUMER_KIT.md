# ExternalOverlayXR 0.1.1 consumer kit

This is the shared Windows x64 OpenXR/D3D11/D3D12 layer, host and widgets.
It contains no frame-generation engine, configuration or loader. It is an
integration kit for a consumer, not an installer or a complete game profile.

Deploy `ExternalOverlayXR.dll` and `ExternalOverlayXR.json` together BEFORE
loading the consumer's startup plugin. Keep the layer name
`XR_APILAYER_EXTERNAL_control_overlay`. Manifest implementation version is 2;
surface ABI and appended information/resolution blocks remain version 1.
Read the integration and first-install guides under `docs/`.

Preserve the consumer's transport, parameter authority, lifecycle and renderer
adapter. Adopt this exact DLL hash from `SHA256SUMS.json`. This kit does not
modify existing installations, process environment, runtime registry or other
API layers. Migration and a coordinated physical VR test belong to the consumer.

Dependencies are installed by the consumer: Windows x64 with the MSVC runtime,
Python 3.12 with Pillow 12.3.0, Node.js and Playwright 1.62.1, and Microsoft Edge.
Use `requirements.txt` and `package.json`; no Python/Node/Edge runtime is bundled.
License requirements are listed in `THIRD_PARTY.md` and `licenses/OpenXR/`.

The example in `examples/demo_host.py` demonstrates an in-memory adapter only.
It does not configure a game, save real render parameters or certify VR/FPS.
Release checks and historical provenance are in the private source repository's
`ENTREGA_0.1.1_r2.md`, `docs/validation/release-0.1.1.json` and
`docs/validation/publication-0.1.1.json`. Carlos accepted this release on
2026-10-07; sources and this kit are available only to authorized users of
the private repository at https://github.com/Beren5556/ExternalOverlayXR/releases/tag/v0.1.1.

AC7 0.8.5-r2 uses this exact DLL and starts its packaged host through relative
profile paths without setup or host.ini. That startup belongs to the consumer's
bridge. Other integrations must implement and test their own automatic startup.
