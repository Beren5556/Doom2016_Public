# Migrating to FoveatedDLSS 1.3.0

Rebuild all consumers with `find_package(DlssFoveado 1.3 CONFIG REQUIRED)`.
API4, schemas1/2/3, control2 and experimental extension1 retain their formats,
enum values and units. CMake uses SameMinorVersion. The additional Image member
and policy helper are source-compatible additions, not a stable binary ABI.
Old releases remain reproducible and the old B3 handoff is unchanged.

## Engine changes

Owned images reuse shader-resource views through weak lifetime tracking; retiring
an Image does not leave its texture held alive by the lookup. External textures
are not retained. Operations is still confined to one immediate/render thread.
`Views()` reports created/reused SRVs and registered lookup entries, not GPU cost.
Expired entries are swept on allocation/access, so registered is not a live count.

Resample uses CopySubresourceRegion only for identical linear floating formats,
single-sample images, equal crop/destination extents and component scale1:1.
Scaling, conversion, sRGB and changed motion-vector units keep the shader path.
Both paths preserve the caller's pipeline state. Composition, binocular masking,
sharpness and history semantics remain unchanged; the rectangle shape option is
not the rejected disjoint rectangular renderer.

When FramePeriod is supplied, Dynamic evaluates consecutive short200ms windows
for sustained overload. Fast intervals after missed deadlines no longer restart
the hold forever. Isolated dips still cannot satisfy the configured hold. The
independent sustainedOverloadMs/recoveryDelayMs remain1000ms by default, JSON-only,
range100..10000. Three prepared sizes, current-frame warm transitions, startup
settling and the existing movement rate are retained. Host pacing intervals may
include waits; they are not GPU busy time or compositor FPS.

Supersampling100→200 doubles central reconstruction output axes with rounding.
It leaves scene input, periphery and final eye size unchanged. It cannot add scene
render detail. Historical NVIDIA evidence is retained under evidence/adoption-b3-0.9.9;
new package tests are CPU/WARP and do not claim new hardware/visor acceptance.

## Opt-in DLSS-only consumer policy1

`dlss_foveado/ConsumerPolicy.h` is separate from generic pipeline capabilities.
Map host render modes explicitly to Renderer; its enum is not a persisted/UI ID.
The B3-accepted policy allows Disabled for all modes, Fixed/Dynamic for SR,
Fixed for Neural, and no foveation for native AA/DLAA. Ordinary DLAA stays on the
host's ordinary route. Other consumers must explicitly choose their own policy.
Generic legacy central-DLAA capability is retained; this helper never activates it.

After validating session, request ID, revisions, configuration and experimental
settings, call CanApply before accepting an enable command. Disabled bypasses
render/projection readiness, but never bypasses authentication, initialization
or optimistic revision checks. Validate again at the render safe point; modes
may have changed since the request. Only invoke FixedPipeline when the resulting
live config.enabled is true, and set PipelineSettings.centralDlaa from the
normalized effective settings. This is a host obligation, not UI-only enforcement.

Call Effective on a COPY of the selected live profile at startup and at the safe
point. It clears the legacy centralDlaa bit and falls back to Disabled for an
incompatible renderer. It preserves contour/settings values and never edits saved
presets. The host retains independent Disabled/Fixed/Dynamic drafts, applied state
and saved presets, and writes only on the existing explicit save operation.
Malformed stored data must still be rejected, not silently normalized as valid.

For Layer control2/ext1, advertise dynamicFoveation, independentModePresets and
stereoGpuInterval only when supported; omit centralDlaa for this policy. Report
dynamicWithNeural=false, dynamicRequiresDlss=true, foveationWithDlaa=false and
the host's own allowed mode IDs. Never copy B3's numeric IDs into another host.
Report recovery availability independently from enable availability. Return
effective snapshots without rewriting stored legacy fields. Preserve request
IDs, revisions and authoritative acknowledgement/recovery for lost responses;
the Layer owns drafts, previews, retries and UI. No new menu controls are needed.

See the installed example and ConsumerPolicy test for the native policy calls.
The host still supplies capture, projections, persistence, transport and runtime.

## Neural runtime and adapter

See [NEURAL_INTEGRATION_310.9.16.md](NEURAL_INTEGRATION_310.9.16.md) and the bundled
integration/neural-310.9.16 kit. No NVIDIA Neural DLL, model or B3 helper is bundled
in the engine SDK. No reintegration into the closed B3 release is performed here.
