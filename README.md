# Doom2016_Public

Community DOOM (2016) VR build based on **KHARVOX 1.11** (internal version 1.1.1), with DLAA, DLSS Super Resolution, fixed/dynamic foveated rendering, an in-VR control panel and optional NR Bridge Neural Rendering.

## Download and install

Download `Doom2016_Public-1.1-dev1-portable.zip` from Releases. Extract the entire archive into a new folder, then double-click `Launch DOOM VR.cmd`. Select your supported Steam DOOM (2016) installation in the KHARVOX launcher and launch from there. Keep all extracted files together. An OpenXR runtime and the game are required separately.

The public package starts with Neural disabled. To use optional Neural processing, supply your own compatible `nvngx_dlssnr.dll` in the installed `NeuralRuntime` folder, set `Enabled=1` in `DOOMNeuralPost.ini`, then restart the game. The local tested build uses model version 310.9.16 compatible with an RTX 4090. This repository does not supply that proprietary model or a patching procedure.

## Added features

- DLAA and DLSS Super Resolution, with adjustable resolution and sharpening.
- Fixed and dynamic foveated rendering.
- In-VR controls for DLAA, DLSS, resolution and foveation.
- Optional Neural Rendering for both eyes before external frame generation.
- Detailed Neural configuration with intensity, style, processing scale, multiple passes and central/peripheral regions.

Neural uses [NR Bridge by LowPopLabs](https://github.com/lowpoplabs/nr-bridge). It runs in a separate worker and exchanges GPU images between Vulkan and D3D12. Its motion vectors approximate headset rotation and its depth is constant. Remleo is an external optional OpenXR layer which consumes frames after Neural. Combined Remleo validation was not separately recorded.

## Version and verification

This build is **1.1-dev1**, validated in-game by Carlos on 27 October 2026. The KHARVOX snapshot is based on 1.11 with local rendering changes. It is a community fork. The official upstream latest release checked on 25 November 2026 is [KHARVOX 1.5](https://github.com/CactusVRStudios/KHARVOX/releases/tag/v1.5.0). This fork has not been rebased onto 1.5.

The engine and Neural components passed 17 offline verification groups including 16 engine CTest regressions. The native/runtime payload passed isolated verification. The portable launcher setup is tested independently without starting the game. Optional bHaptics software is obtained separately.

## Sources and build

Engine, renderer, overlay, Neural adapter, packaging tools, tests and compiler dependencies are included in this tree. See `BUILD.md`. Third-party source keeps its own license and provenance. The binary release contains compiled shaders and the permitted runtimes, while the proprietary Neural model and bHaptics runtime are supplied by their respective owners/users.

## Credits

[KHARVOX by CactusVRStudios](https://github.com/CactusVRStudios/KHARVOX), MIT license, copyright 2026 CactusVRStudios. Native stereo code derives from dbkni's MIT-licensed implementation. [NR Bridge](https://github.com/lowpoplabs/nr-bridge), MIT license, copyright 2026 LowPopLabs. SFS profile provenance is credited to Octavian Mihai Vasilovici (Helifax), [Vk3DVision-Public](https://github.com/helifax/Vk3DVision-Public), with its BSD 3-Clause notice retained with the binary profile. NVIDIA, Khronos, AMD, Microsoft and other dependencies retain their own notices. See the bundled third-party notices. No endorsement by the original authors is implied.
