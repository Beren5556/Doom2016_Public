# Compatibility and ownership

Current1.3.0 migration and opt-in consumer policy: [MIGRATION_1.3.0.md](MIGRATION_1.3.0.md). Generic central-DLAA capability below is not enabled by the DLSS-only consumer policy.

Current package: FoveatedDLSS 1.3.0, accepted reusable improvements from B3 0.9.9.
Base source contract 4, schema 3 and control contract 2 retain their meanings.
The separately negotiated experimental extension is version 1. See
[extension and migration](EXPERIMENTAL_CONTRACT_1.0.md).

| Identity | Value | Rule |
|---|---|---|
| Engine implementation | 1.3.0 | Pin source commit and package SHA256 |
| C++ source contract | 4 | Recompile consumers/backends; not a binary plugin ABI |
| New configuration | 3 | 22 semicolon-separated unsigned integer fields |
| Configuration read | 1, 2, 3 | v1/24 and v2/30 keep Legacy geometry; v3/22 uses four sides |
| Layer control contract | 2 | CONTROL_CONTRACT_2.0.md; host adapter required |
| Experimental extension | 1 | Explicit opt-in; Dynamic and complementary central DLAA |
| GPU backend | D3D11 | No D3D12/Vulkan/arbitrary-game compatibility claim |
| Placement | Fixed | No gaze provider |

Keep DlssFoveado, dlss_foveado::*, enum values, basis-point units and the host's
foveation-v1.ini name. CMake uses SameMinorVersion; consumers of this package ask
for `find_package(DlssFoveado 1.3 CONFIG REQUIRED)`. Existing 0.3/1.1/1.2 CMake requests do not
silently accept 1.3; update the request and recompile all consumers/backends. The imported 0.3.0 source and original receipt remain preserved
in commit 7e8327b6dc91b987781e6ed47243a57d1d1327cc and evidence/origin.

The new default Config requires a host-supplied binocular projection for each
view. It does not infer calibration from NasalSide, view IDs or headset names.
Schema 1/2 read and Encode retain legacy geometry and schema 2, including all
old controls. FourSideDraft explicitly proposes the new mode, preserves shared
settings and initializes both sets of cuts to 20%. There is no general exact
conversion of the old nasal/offset geometry. Applying the draft changes mode;
ApplySave writes schema 3. New Layer sessions never send old-schema commands.

Legacy Region fields and eye centers remain C++ compatibility fields, ignored
in BinocularFourSides; use the new fields only in that mode. They are absent from
schema 3 and are reset to defaults on Parse. RegionPlan centerPixels,
halfSizePixels, transitionPixels and nasalSide describe Legacy only. In the new
mode its normalized binocular map, T/B/R/L radii and transitionFraction are the
mask authority. cropScale/cropBias remain output-pixel-to-cropped-UV in both modes.

The host owns temporal guides, verified projection conventions, spaceId/revision,
device/context, NGX runtime/cache/project ID, publication/fallback, persistence
and optional Neural provider. The Layer owns UI and transport. references/bl3 is
historical context, not an updated host. Geometry/config changes need a new epoch
as described in README; the extension permits automatic central-cut motion
inside prepared bounds without a new epoch. Indicators remain outside history.

B3 0.9.7 integration is owned by its separately authorized coordinator. This
project does not modify or install B3. Exp6 feedback is historical host evidence,
not proof for every headset/runtime. Offline tests establish CPU/WARP behavior
and relocated consumption; NGX is built and linked, without real inference or
a new headset test. No game/runtime is started here.
