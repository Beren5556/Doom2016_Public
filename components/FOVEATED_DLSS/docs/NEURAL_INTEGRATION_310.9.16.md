# Host-supplied Neural310.9.16 integration contract1

The accepted input is ShortFuse compat-fp16-310.9.16-cuda13.4.3 PATCHED:
SHA256 CDFA6C1EC784867182164C7836F324F087B5C9F374D0C9FE46F54999FEABF3ED.
It remains available as NeuralInput in the private B3 v0.9.8 release:
https://github.com/Beren5556/B3VRDLSS/releases/tag/v0.9.8
Use B3 v0.9.9 sources for its corrected host/helper, pinned to
4a4bd651d1a8bba491a099cc39a73fbf85fcc636. Its source paths below are references,
not dependencies of this engine. Runtime redistribution conditions remain with
the host; this SDK does not bundle or license the library, proxy or private model.

## Reusable packaging input

The kit ships a machine-readable contract and PowerShell7 preparation script:

```powershell
./integration/neural-310.9.16/Prepare-NeuralRuntime.ps1 `
  -Library C:/inputs/nvngx_dlssnr.dll `
  -Proxy C:/inputs/consumer-proxy/nvngx_dlssnr.dll `
  -ProxySha256 <your-verified-proxy-SHA256> `
  -OutputDirectory C:/staging/new-neural-input
```

The output directory must not exist. The script checks the pinned real-library
hash and caller-pinned proxy hash, rejects a real library supplied as proxy, and
copies both into left/right directories. `nvngx_dlssnr.dll` is the proxy;
`nvngx_dlssnr_real.dll` is the real library behind it. Every output is rehashed.
No DLL is loaded, no process is started, no installed profile is overwritten.
The receipt proves bytes and layout, not proxy compatibility or inference.

This is a two-eye packaging INPUT, not a complete installer or working helper.
The consumer must supply its proxy, helper/model/dependencies, translate these
directories to its own per-eye layout, and regenerate its versioned full bundle,
resource manifest and installer identities. Never update only one checked DLL
inside a bundle or copy another PC's installation manifest/absolute paths.

## Host adapter obligations

- Match the real full-route contract before aligning regional NR. The accepted
  B3 capture uses DLSSNR.Hint.Render.Preset=0 and DLSSNR.UICorrection=0 regionally;
  the full route has preset0 and UI correction0 on evaluation (absent at creation).
  Both regional keys changed together. This is not SR presetK, and does not prove
  either key alone caused the improvement. Do not apply0/0 to a consumer with a
  deliberately different full-route contract without checking it.
- Inherit normal APPDATA, LOCALAPPDATA and USERPROFILE for the auxiliary process.
  Keep private TEMP/TMP, private working directory, hidden process/job ownership,
  XR environment filtering and transport validation. Preserve the original30s
  deadline. Do not restore the abandoned preload or120s timeout workaround.
- Eliminate an output-fence GPU Wait only when the SAME fence/value has already
  been proven complete by the CPU check. Preserve input Signal/Flush, the helper's
  input Wait, output validation, exactly-one-successful-evaluation counters, final
  copy completion and resource lifetime. A timeout/error is not completion.
- Preserve independent eye epochs, receipts, actual extent/order/format/guide
  validation and Neural-before/after behavior. Dynamic+Neural remains excluded.
- Keep generated host/helper sources coherent with generator inputs and receipts.
  In B3 the reference is PrivateRegionalNeural.h, PrivateNeuralHost.h,
  derive_neural_host.py and derive_neural_helper.py. Other consumers own their own
  generation and bootstrap; these B3 files are intentionally not engine sources.

NeuralRegionBackend remains the engine/provider boundary. It carries images,
guides and receipts, not vendor presets, process environment or B3 paths. This
release does not remove synchronization or change NR presets inside that generic
interface; those fixes belong where the consumer implements it.

## Validation and evidence

CPU/WARP tests validate the engine and policy. Kit tests check the exact real DLL
as bytes, a non-executable proxy fixture, both eyes and negative identity/overwrite
cases; they never execute NR. Each real host must test its actual proxy/helper,
two eyes, size/order changes and evaluations, then visual stability in its target.
Carlos accepted B3's combined correction. That acceptance supersedes historical
pending B3 trial notes, not new-consumer tests. GPU/CPU/wait times and evaluation
counts are distinct; synthetic measurements are not game/compositor FPS.
