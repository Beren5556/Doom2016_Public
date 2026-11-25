# DOOM temporal adapter: projection evidence and caller-owned DLSS commands

18 October 2026. Local pilot, based on KHARVOX v1.1.1. The installed game still uses the validated temporal-audit DLL. The changes below extend the laboratory provider; they do not enable DLSS in the game.

## Projection and filter evidence

The installed DOOM executable is fingerprinted before static inspection. Evidence lives in the parent workspace under `evidence/20261018/temporal-contract`; no game process is launched by the inspection.

The generator at RVA `0x1830f70` has a normal branch that computes radical inverses with base 3 for X and base 2 for Y, using a byte phase plus one. It subtracts one half and scales by the near-plane frustum span divided by the raster dimensions passed by its caller. A separate branch handles a sample grid. A global gate can zero the offsets. The builder at `0x1830980` can also use an explicit projection instead of this generator.

The perspective helper at `0x2c42d0` adds the offsets to both left/right and bottom/top planes. The matrix helper at `0x2c41e0` places their normalized sums in projection coefficients. The model-matrix function at `0x1ade630` calls the generator conditionally, including its jitter argument and a model flag. These paths establish where offsets can enter geometry; they do not establish which branch every draw in the captured scene took. The input structure pointer is not assumed identical across callers.

The 64 GPU samples from the real VR capture match a normalized Gaussian nine-tap low-pass kernel with exponent factor 8/9 and center equal to the negative generator offsets. All eight phases match, with maximum coefficient error `4.081982343806523e-08`. The captured order starts at Halton index 6, then 7, 8, 1, 2, 3, 4, 5; it advances with the 32 consecutive SFS serials. The counter's eight-phase wrap is observed, not assumed from the generator's byte arithmetic.

`scripts/analyze_doom_filter_phase.py` in the parent workspace reproduces the match and rejects unrecognized weights. It preserves `rasterJitterQualified=false`. Projection-generator offsets must not be passed directly to NGX as final jitter: actual raster branch, viewport Y orientation, SFS per-eye projection scaling and current-frame association remain to be established. The sampled frustum and previous-camera uniforms do not certify the jittered geometry transform.

## Color, depth and exposure boundary

The actual TAA color view is B10G11R11_UFLOAT_PACK32; the provider already accepts that format. Actual depth is D24_UNORM_S8_UINT. The existing temporal-guides shader extracts selected device depth to R32F and corrected camera/object motion to RG32F for the provider. The packed second output of the original TAA resolve remains unsuitable as XY motion.

The TAA compression `HDR / (128 + luma(HDR))` and its inverse are a filter working-space transform. They do not establish upstream exposure or preexposure. All 75 installed profiles were decoded locally; that replacement set is not an inventory of every shader used by the game. Its post-exposure symbol belongs to a vertex effect and does not qualify the TAA input scale.

The pinned NGX Vulkan helper distinguishes the exposure texture, preexposure and exposure scale. The laboratory uses NGX autoexposure. That handles its own exposure calculation but does not identify an unknown upstream color multiplier. The provider's temporal-contract guard remains mandatory. There is no newly claimed game exposure value and no runtime activation based solely on the filter match.

## Recording DLSS at the host's insertion point

`VulkanDlss::Record` accepts the host's open primary command buffer, outside a render pass and on the same device/graphics+compute queue family as the provider. It records creation/evaluation and resource barriers without beginning/ending that command buffer, submitting it, or waiting for the GPU. The host serializes provider/NGX calls and synchronizes the shared queue.

This allows a future host to record:

1. Its producer render pass and temporal-guide pass.
2. DLSS evaluation after those passes, before the TAA replacement/consumer.
3. Output consumer operations with explicit access/layout barriers.
4. One host-controlled submission and a fence covering the last reader.

The existing `Submit` API remains available for laboratory work. It requires previously submitted inputs; `Record` can consume inputs produced earlier in its same command buffer. This API change does not itself discover the game's pass boundary, resolve descriptor identity, negotiate device extensions or replace the game's TAA outputs.

### Ownership rules

- `Recorded` supplies a ticket and private GPU output references. It does not mean CPU-ready. `Poll` cannot expose them as `Ready` before a host completion.
- Every nonzero ticket requires `CommitRecorded(ticket, lastReaderFence)` after submission, or `CancelRecorded(ticket)` after all referencing command buffers are discarded/reset and guaranteed never submitted.
- A partial recording failure returns a ticket with no published eye images. Its private resources survive until cancellation or fence retirement. Both histories are then discarded.
- While a ticket or consumer remains outstanding, both `Record` and `Submit` return `Busy`. Duplicate frame/epoch identities remain rejected. Canceling an unsubmitted frame does not consume its identity.
- The borrowed fence must remain alive and signaled until the next `Record`/`Submit` has observed it. The host must resolve outstanding tickets before destroying the provider. It must preserve the provider's declared output layout or transition back before reuse.
- Callers provide valid command-buffer state, device, queue family, pass boundaries and last-reader coverage. These are API obligations, not properties the provider can infer from opaque Vulkan handles.

## Validation

The original GPU suite still passes 96 real NGX evaluations. The new suite verifies 82 eye outputs, including DLAA/SR, separate images and array layers, resizing, repeated frames, private eye histories, cancellation/retry, allocation failure after eye zero has recorded, retirement of a partial submission, held consumer fences and transitions between both APIs.

The first frame of each case creates/clears input images, records NGX and copies both outputs to host buffers in the same command buffer. Real GPU readback verifies finite pixels and distinct eye colors. The provider dispatch is observed during `Record`; no begin/end, queue submit, device-idle or fence-wait calls occur through those entry points. This is functional synchronization evidence, not a performance measurement.

The tested actual NGX module remains the existing driver override, version 310.9.16, rather than the requested SDK runtime 310.9.1. No driver profile or global runtime is changed. No Khronos validation-layer run, live game DLSS evaluation or visor quality/FPS result is claimed.

## Remaining integration work

The latest geometry session associates the engine's view-projection with 704 static-world eye pairs in 11 frames. Reuse that result; do not repeat the earlier general association test. Define preexposure at the real color producer, address the observed jitter amplitude coherently with both eye projections, and complete the producer/consumer dependencies at the pre-TAA boundary. Then use `Record` at that boundary and publish its result through a replacement resolve preserving downstream outputs. Start with constant-resolution DLAA. Any additional physical test must answer a specific unresolved question and be coordinated with Carlos.

### Reusable NGX device preparation

`VulkanDlssRequirements` copies NGX instance/device extension requirements using the same runtime/cache/project/engine identity as initialization. It verifies advertised revisions, merges without duplicate additions, and retains the original host list on any missing requirement. Enumeration retries `VK_INCOMPLETE` at most four times. The host must include extensions supplied by its enabled layers and account for API-version-dependent extension dependencies.

The discovered KHR buffer-device-address requirement has a separate feature plan. It queries support and adds only `bufferDeviceAddress`, retaining legacy core flags, queues and the complete host pNext chain. Existing standalone/Vulkan1.2 declarations must already enable it; conflicting EXT, duplicate declarations and cycles are rejected. Plans own their inserted structure and must stay alive through creation; ordinary and OpenXR-mediated create chains need separate plans. This is preparation, not automatic live DLSS activation.

The real-GPU fixtures now use this module instead of private NGX queries or enabling all supported Vulkan1.2 features. The provider tests create a Vulkan1.2 device with zero core feature bits plus buffer device address; the stereo chain preserves its explicit SFS features and adds only that address feature. Tests pass: 96 NGX evaluations, 82 recorded eye outputs, and 491,520 chain eye pixels with exact packed RG preservation. The 16 chain/regression tests and 3 requirements/provider tests pass; evidence is `evidence/20261018/dlss-requirements/minimal-features` in the parent workspace. The current driver/runtime is qualified only for these fixtures; production negotiation and live temporal inputs remain separate work.

## Operation identity at the temporal consumer

The opt-in audit now uses schema 2. Binding 32 is a private dynamic storage buffer. At each recognized stereo TAA draw, the wrapper appends one private offset after the original dynamic UBO offsets, selecting storage owned by that recording. It does not modify the public pipeline layout or game push constants. Dynamic offsets follow [Vulkan's set/binding/array ordering and effective-address rules](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html).

The fragment copies its 64-bit operation token along with the original constants and eye index. CPU metadata includes command-buffer generation and draw ordinal, pipeline shader hashes, render pass/subpass, framebuffer attachment identities, render area, static or dynamic viewport/scissor, descriptor allocation generation and the original UBO offsets. The execution frame is read by the GPU; `recordedFrame` is explicitly not substituted for it when an executable command buffer repeats.

Storage remains owned until command reset/re-record/free/pool destruction, and is recycled only after the existing device retirement and evidence readout. Resetting a completed command before reading the evidence does not overwrite its earlier sample. There are at most 320 slices shared by descriptor bookkeeping and recorded operations. Exhaustion selects disabled slice zero and is reported. The 32 observed-interval limit, arm marker, shader-to-host barrier and existing device-idle retirement remain. No per-draw submit/wait is introduced.

The runtime fixture executes the original game fragment through NativeSfs with synthetic inputs. All 34 observed operation pairs have matching GPU/CPU tokens, resource metadata and effective UBO offsets. It covers two draws in the same command/set with different offsets and constants, static and dynamic raster state, re-execution of an unchanged command on another frame, and reset/re-record before evidence retirement. Separate capacity checks exercise exhaustion and reclamation. The unchanged resolve's defined channels remain bit-identical. The SFS regression passes 13/13 tests; SPIR-V validation passes. The analyzer additionally rejects ten ambiguous/missing-data cases and still decodes the prior 64 VR samples without inventing operation identity for them.

The subsequent coordinated VR session `live-temporal-operations-20261018-200552` validates the association mechanism in the game: 32 pairs/64 GPU records, all with matching operation tokens, stable descriptors and effective dynamic UBO offsets. GPU frames 1800–1831 are consecutive; their CPU recording serials are one lower. All recorded TAA draws use dynamic viewport `(0,0,4000,4250,0,1)` and scissor/render area `(0,0,4000,4250)`. Both eye histories match the previous current SFS transforms bit for bit over 31 transitions. The filters again match eight phases, starting at index 7, with maximum error `4.081982343806523e-08`.

For all 31 consecutive transitions, the current history input matches a subresource in the previous framebuffer (index 0). The two history image identities alternate. The observed color attachment has format B10G11R11_UFLOAT_PACK32 and usage 20 (sampled/color attachment); the other attachment has RG16F and usage 28 (sampled/storage/color attachment). This is an identity chain, not proof of the entire submission dependency graph. Raw framebuffer attachments are recorded in framebuffer order; no subpass output-index interpretation is inferred from that order. Their `declaredLayout=0` is an unused descriptor placeholder, not an observed transition to UNDEFINED. Only four core draw entry points are tagged; unrecognized paths remain unqualified.

The live result establishes the TAA viewport, not the earlier geometry's applied jitter. HDR preexposure, geometry operation association, producer graph and game DLSS execution remain unresolved. The capture stopped automatically without capacity exhaustion, and Carlos closed the game. Reports, checks and raw hashes are in the parent workspace session directory. Do not repeat this test just to reconfirm operation identity.

## Private DLAA color consumer

`DoomTemporalResolve.h` adds an explicit `temporalResolve` compiler option. It is mutually exclusive with audit/guides, requires the recognized corrected stereo fragment profile, and rejects an occupied private descriptor set or changed packed-output channel contract. Ordinary SFS compilation never selects it.

The private pipeline has original game/SFS resources in set 0 and separate one-layer `sampler2D` views for left/right DLSS results in set 1, bindings 0/1. It fetches the appropriate eye's RGB at `ivec2(gl_FragCoord.xy)` and writes output location 0, without the TAA color filter or history blend. It retains the corrected original `computeVelocity` and `OutputVelocityTarget` functions to write reciprocal blur magnitude and linear depth at output location 1. NGX's previous-minus-current pixel-motion and device-depth guides cannot substitute for that packing.

This shader is limited to initial native-resolution DLAA with equal per-eye dimensions and a positive full-frame viewport starting at zero. The host must validate sizes, frame/eye identity, subpass attachment mapping, layout/access synchronization and lifetime before using it. It must own the private pipeline/layout and preserve or restore the game's binding state. SR, crops, negative viewport height and a live render-pass insertion point are not supplied by this shader.

Raster output avoids requiring storage or transfer-destination usage on the game's history images. The real-GPU fixture creates the exact observed output formats/usages (122/20 and 83/28), samples them into separate readback targets, and compares with the complete original TAA shader. Four scenarios cover static camera, object movement, distinct previous-eye transforms and nonuniform depth. All 512 eye/pixel positions receive the expected nonuniform RGBA16F source color with no flips or cross-eye mixing; both defined packed RG channels match the baseline bit for bit after RG16F conversion. Zero-motion infinities are preserved, and NaNs are rejected by the fixture. Six rejection checks cover missing correction, changed reciprocal/channel packing, private-set collision, mixed variants and mono use.

The SFS suite passes 14/14 and guide/audit/resolve SPIR-V validates for Vulkan 1.1. These consumer inputs simulate DLSS output to make exact pixel assertions; this test does not evaluate NGX or qualify live jitter/exposure. The earlier provider tests remain separate evidence. The installed live-validated audit DLL is retained; building this consumer does not enable DLSS or change the game installation.

## Opt-in startup and direct engine inspection

`VulkanDlssStartup` preserves the application's API version and both ordinary/OpenXR device-create chains. It closes the reviewed NGX extension dependencies, including device-group creation/group on Vulkan 1.0. Unknown requirements reject optional preparation. A layer built with the DLSS pilot only prepares NGX when `KHARVOX_DLSS_PREPARE=1` and absolute `KHARVOX_DLSS_RUNTIME`/`KHARVOX_DLSS_CACHE` paths are provided. Its startup probe initializes/queries/shuts down NGX without feature creation or evaluation. Vulkan 1.0/1.2 GPU tests verify separate chains and no work submission/wait through the observed dispatch. The game remains on its original SFS renderer; live NGX startup is not yet validated.

A layer built with `KHARVOX_RENDERDOC_DIRECTORY` supports an explicitly armed, single RenderDoc frame. `SourceRing` retires images through submits rather than a downstream present, so ordinary RenderDoc hotkey capture may never reach a frame boundary. `RenderDocFrame` calls the official API with `RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE` at two consecutive successful source-present boundaries. It waits for `<KHARVOX_RENDERDOC_CAPTURE>.arm`, saves `.done` only after the RDC exists, and never rearms. Without that absolute environment prefix it is inactive. No extra image readback or shader instrumentation is added by this bridge.

The portable RenderDoc 1.46 fixture verifies arm/end/one-shot behavior, original stereo color/packed outputs, and automated replay. Exact consumed UBO values and 256 eye-depth pixels survive capture/replay. Discovery with KHARVOX before RenderDoc is verified by the loader trace using process-only `VK_ADD_IMPLICIT_LAYER_PATH=<mod>;<RenderDoc>`. No system registration is required; see [loader discovery/filtering](https://github.com/KhronosGroup/Vulkan-Loader/blob/main/docs/LoaderLayerInterface.md). Runtime VR interoperability remains a joint test, not a laboratory claim.

Parent-workspace tools: `scripts/renderdoc_trial.ps1` defaults to read-only Preflight; Launch requires a new session directory, Arm requires Carlos's gameplay confirmation, Status verifies the RDC, and Analyze replays only after the game closes. `analyze_renderdoc_frame.py` extracts engine shader constants/resources around HDR/exposure/TAA draws. The next targeted test is one gameplay frame to inspect the real engine exposure multipliers and HDR dependencies. Keep NGX preparation off during this RenderDoc test. Neither post-tonemap exposure nor a successful synthetic replay establishes the game's DLSS preExposure.
