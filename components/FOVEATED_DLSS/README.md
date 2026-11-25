# FOVEATED_DLSS

Current1.3.0 migration and opt-in consumer policy: [MIGRATION_1.3.0.md](docs/MIGRATION_1.3.0.md). Generic central-DLAA capability below is not enabled by the DLSS-only consumer policy.

Private standalone project for reusable foveated DLSS with fixed placement.
Fixed and opt-in Dynamic modes share the accepted binocular geometry.

Current engine: **FoveatedDLSS 1.3.0**, adopting the accepted reusable B3 0.9.9 crop/view/controller improvements
and providing the opt-in consumer policy and patched Neural integration kit. It follows the published private 1.1.0
and preserves the original 0.3.0 baseline. Start with [PROJECT_STATE.md](PROJECT_STATE.md)
and [docs/HANDOFF_1.0.md](docs/HANDOFF_1.0.md).

The project/repository name is FOVEATED_DLSS. Existing C++/CMake identifiers
`dlss_foveado` / `DlssFoveado` are preserved for compatibility with the accepted
BL3 host; renaming those is not part of this transfer.

Engine version and host release version are independent.
Engine 1.3.0, source contract 4, configuration schema 3 (reads 1/2/3), control 2,
opt-in experimental extension 1. A standalone
CPU contract test does not establish NVIDIA inference or headset compatibility.

## Boundaries

The core has no BL3, Unreal, UEVR, OpenXR, SDK paths or game resource discovery.
Input views have stable identities and already-decoded temporal guides. The
host owns atlas extraction, camera conventions, configuration persistence, UI,
runtime discovery and final publication. The D3D11 backend is a separate target;
being reusable across projects does not mean API-agnostic GPU support.

Public configuration uses a bounded, versioned encoding. Unknown versions or
invalid fields are rejected. No persisted gaze data and no eye-tracking provider
are shipped; both processing modes keep the center fixed. Region placement is an independent input
so future gaze can use the same geometry/indicator contract.

## Updating the engine

1. Keep the engine version, public contract version, configuration schema and
   host release version separate. A behavioral update with a compatible public
   contract should not require edits to host capture or the Lua UI.
2. Update the behavioral reference in `provenance.json` only after reviewing the
   upstream changes. This is original implementation inspired by functionality;
   it neither embeds Cheeky's plugin nor permits automatic source copying.
3. Preserve published enum meanings, units, ownership and failure semantics.
   Add capability negotiation for new processing modes. Incompatible schemas
   need explicit migration or rejection; never silently reinterpret old fields.
4. Build the isolated consumer, run analytic geometry/temporal contract tests,
   GPU tests for changed backends/shaders, and the host regression tests against
   the exact candidate. Headset and performance acceptance are separate.
5. Package only declared dependencies and retain reproducible shader sources,
   compiler settings and identities. Do not fetch a floating upstream branch
   or replace binaries from the runtime UI.

BL3, NGX SDK/runtime and future eye-provider adapters have independent lifecycle
and licensing constraints. Those constraints remain with each adapter instead
of leaking their paths and structures into the public core.

## Independent build and consumption

Copy this directory to another project. CMake 3.24+, C++20 and, for the GPU
targets, Windows/MSVC x64 and the Windows SDK are required. The CPU target has
no NVIDIA dependency. NGX is opt-in via an explicit SDK directory; its SDK and
runtime have separate NVIDIA licensing/distribution requirements.

```powershell
cmake -S . -B build -A x64 -DDLSS_FOVEADO_NGX_SDK=C:/sdk/NVIDIA-DLSS-310.9.1
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix C:/sdk/DlssFoveado
```

Consume `find_package(DlssFoveado 1.3 CONFIG REQUIRED)` and link
`dlss_foveado::core`, `dlss_foveado::d3d11` or `dlss_foveado::ngx`. If the
installed package contains NGX, pass the consumer's `DLSS_FOVEADO_NGX_SDK`;
exported targets contain no originating workspace path. See `examples/consumer`.
For core only, set `DLSS_FOVEADO_BUILD_D3D11=OFF` and leave the NGX SDK empty.
`tests/NgxHardware.cpp` is an explicitly invoked GPU test, not an automatic CTest.

## Host contract and ownership

1. Resolve the host's device, runtime/cache directories and a valid NGX project
   UUID. Retire the host's other NGX session before `CreateNgxBackend`; the
   default provider supports one owned NGX session per process. Validate
   `Capabilities()` before selecting features. Current capability claims cover
   LDR SR and LDR native AA; HDR native AA is not advertised by this pilot.
2. Parse/validate `Config` (disabled by default). Basis points mean 10000 = 100%.
   Schema 3 uses four independent cuts, each 0–4500, measured against a common
   binocular diameter. Peripheral scale uses input dimensions; supersampling
   enlarges the central reconstruction destination. Parse of schema 1/2 keeps
   `GeometryMode::Legacy`; call `FourSideDraft()` only to propose explicit migration.
   Central preset 0 inherits the host setting; peripheral preset 0 is NGX Auto.
3. Call `Plan` with stable, distinct view IDs and per-eye input/output extents.
   Contract 4 requires `ViewGeometry::binocular` for the new default mode. Each
   row-major homography maps unjittered eye-local output pixels to a shared plane
   with equal X/Y units and center (0.5,0.5). The host supplies verified projection
   data and the same nonzero spaceId/revision for both views. Missing calibration
   is rejected; `nasalSide` is not used as a substitute. See
   [control contract 2](docs/CONTROL_CONTRACT_2.0.md) and the synthetic example.
   Use the returned plan unmodified. Epoch/config/frame IDs are positive and
   frame IDs increase inside an epoch; resource/projection/camera discontinuity
   requires an epoch change and reset. A future gaze provider must also define
   moving-crop history/reset and validity semantics; placement is fixed today.
   For four-side mode, any configuration change except indicator visibility or
   selection, projection/view/size change, placement revision, Neural order/mode,
   inherited preset or central quality change requires a new epoch. Begin checks
   this and forces reset for both eyes/providers on the first pair of an epoch.
   The opt-in Dynamic exception permits automatic central-cut motion within
   prepared bounds without changing the epoch; configuration, projection and
   real camera changes still reset. See the extension contract below.
4. Optionally prepare `NeuralRegionBackend` using `NeuralRegions`. The provider
   returns a complete region at native resolution with a matching receipt.
   It owns any private resources, model/driver requirements and asynchronous
   startup. Preparation occurs outside `Evaluate`; no ready provider means the
   host must keep its ordinary image. No game-specific Neural provider ships
   inside this reusable module.
5. `Begin`, then `Evaluate` left/right in order on one serialized render path.
   Textures/context must belong to the same device. Color is encoded LDR;
   depth is inverted eye-local R32 float; motion is decoded eye-local floating
   point, previous-minus-current displacement in input pixels, without jitter.
   Jitter is in input pixels and frame time in milliseconds. Color may have an
   explicit atlas origin; guides are eye-local. No MSAA or implicit resolve.
   The provider has independent histories for each view/branch.
6. Validate `CanPublish` and `Complete`. An eye failure invalidates the pair;
   `Abandon` invalidates receipts. The host owns whole-pair rollback/publication.
   Keep a clean checkpoint before `DrawIndicators`; call the latter only after
   both eyes complete, once per frame. Diagnostics must never feed histories.
   Returned scratch images are borrowed across calls and may be reused on the
   next frame; copy when retaining output longer. Sharpen is a final spatial pass.
7. At reconfiguration/shutdown, keep the provider and device/context alive and
   call `Stop(context)` until true on later render iterations. It does not
   spin-wait; completed GPU queries permit releasing retired features. Do not
   destroy a busy backend or unload its runtime. The defensive destructor may
   retain resources rather than free in-flight features; that is not a shutdown
   strategy. Destroy the pipeline before its reconstruction provider.

The C++ contract is a source API, not a binary plugin ABI: rebuild consumers
when changing compiler, SDK or engine. Exceptions report invalid input/backend
failure; no partial-eye success or silent mode downgrade is a valid receipt.
The host chooses its recovery UI, fallback and persistence policy.

## Opt-in extension in engine 1.3.0

[Extension contract 1](docs/EXPERIMENTAL_CONTRACT_1.0.md) defines Dynamic,
independent mode presets at the host, bounded T/B/R/L contours, prepared central
contexts, transition history, native central DLAA with peripheral SR and the
JSON-only waits. Neural + Dynamic is rejected; Fixed + Neural is preserved.
Base API 4/schema 3/control 2 are unchanged. Recompile all consumers/backends;
this is not binary compatibility with 1.1.0. CMake requires version 1.2.

The host supplies fresh asynchronous timing and runtime cadence. The engine does
not discover a game or headset. Automatic changes keep the original geometry
and the same per-eye maps. See the extension for preparation and reset rules.

## Four independent sides (introduced in engine 1.1.0)

Cut percentages describe one common contour, never two independently centered
circles. With ellipse shape, 20/20/20/20 is a circle of 60% diameter; 20/20/0/0
is a horizontal ellipse of 100% width and 60% height. Unequal opposing cuts use
four quarter-ellipses around the fixed common center. Rectangle uses the same
four bounds. The transition expands each corresponding radius. CPU planning,
D3D11 composition and the diagnostic outline use this same projected contour.

Each eye's crop encloses the projected region plus transition and a one-input-
texel numeric guard. Homogeneous half-plane clipping keeps it conservative for
asymmetric and projective maps. Too-small or off-view crops fail backend limits;
the host retains its ordinary pair and reports why. Neural-before scales its
input pixel coordinates into the output plane; Neural-after uses output pixels.

The host must establish the real mapping/calibration and validate it physically.
The example consumer supplies synthetic overlapping maps, not headset values.
`FourSideDraft` retains shared quality/shape/transition settings and proposes
20% per side for legacy configurations. It does not claim visual equivalence.
Only applying the draft changes mode; only ApplySave writes schema 3. Details:
[control contract 2](docs/CONTROL_CONTRACT_2.0.md).

## Legacy continuous nasal oval (engine 0.3.0 / schema 1 and 2)

The following behavior is retained only by `GeometryMode::Legacy`. The API names
remain for old configurations; the new Layer menu does not expose these controls.

This replaces the rejected 0.2.0 half-plane union: no center-X straight cut.
For a nasal view, widthBps is retained horizontal coverage of one eye. At neutral
placement the oval center is at the nasal image edge and horizontal radius is
viewWidth * widthBps / 10000. The two mirrored open curves describe the approved
combined-field sketch; the indicator never draws a closing line at the seam.
Rectangle mode uses the same bounds, with its explicitly rectangular outline.

Height is full-field when lockHeight is true (the new default). Otherwise:

- reduction = (1 - widthBps/10000) * verticalRatioBps/10000;
- top = reduction * upperShareBps/10000; bottom = reduction - top;
- vertical center = viewHeight * (1 + top - bottom)/2;
- vertical radius = viewHeight * (1 - reduction)/2.

80% retained width, 90% vertical ratio and 70/30 distribution therefore retain
82% height, with 12.6% removed above and 5.4% below. A ratio of zero preserves
height; 100% matches horizontal proportional reduction. Upper/lower always sum
to 100%, storing only upperShareBps. Locking does not erase ratio/distribution.
This applies separately to central and independent Neural regions; linked
Neural takes all central geometry parameters.

Advanced placement offsets move the nasal anchor by (centerX - 0.5)*viewWidth;
unlocked Y adds (centerY - 0.5)*viewHeight. Neutral 0.5/0.5 matches the sketches.
Locked height ignores Y to retain the complete vertical axis. Displacing the
centers independently deliberately changes alignment; it is not eye tracking.
The non-nasal reusable mode still uses legacy linkedAxes/heightBps semantics.

Composition, indicator and real crops derive from the same ellipse/rectangle.
Crops include transitions and clip to each eye; they do not stretch old regions
over new coverage. Neural-before/after use the same output-space geometry with
explicit coordinate scaling. This is image-space geometry, not an optical
projection/IPD model. Physical binocular perception remains a host/headset test.

Source contract 3 requires consumer/backend recompilation. Schema 2 appends six
fields to the original 24: central lock/ratio/upperShare, then Neural equivalents.
The legacy parser accepts exactly v1/24 or v2/30 fields. Legacy v1 migrates in memory
with height locked, ratio 100%, split 50/50, retaining all old numeric settings.
The host retains the filename foveation-v1.ini; it writes v2 only on ApplySave.
Existing size values now mean retained eye-field width in BL3 rather than the
old circle diameter. This intentional geometry correction is not identical
visual restoration of the rejected mask. Unknown versions/invalid fields fail
closed. Ordinary DLSS controls and files are independent.

## Update acceptance

For an engine update, review `provenance.json`, build this isolated package and
consumer, exercise geometry/configuration, D3D11 crop/composition/state restore,
stereo tickets/failure recovery, then run real NGX/optional NR with exact shipped
dependencies. Host-specific capture and headset acceptance remain required.
Archive candidate hashes and results. Do not equate successful compilation or
synthetic inference with faster rendering, physical stereo alignment or gaze.
