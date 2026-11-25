# Building

Use Windows x64, Visual Studio 2022 C++ Build Tools, CMake and .NET Framework 4.8. The source trees retain the tested compiler snapshot and notices. The toolchain snapshot has historical provenance limitations described in the upstream notices.

Obtain NVIDIA's official DLSS SDK separately from https://github.com/NVIDIA/DLSS and place the SDK at `components/FOVEATED_DLSS/dependencies/SDK/NVIDIA-DLSS-310.9.1`, or pass its location through `KHARVOX_NGX_SDK` when configuring CMake. Retain NVIDIA's license. The historical configuration uses SDK 310.9.1. Obtain the OpenXR loader from Khronos or a permitted KHARVOX runtime and pass `KHARVOX_OPENXR_LOADER`.

`scripts/build_neural.ps1` shows the engine/component build options. Its Runtime argument points at an existing runtime containing DLSS and the OpenXR loader. The native engine can be configured directly using CMake and the paths above. The optional model is not necessary to compile. Tests exercising the real model require your own compatible runtime.

`development/neural-post` builds DOOMNeuralPost.dll, DOOMNeuralWorker.exe and the NR Bridge forwarder. Its ABI retains historical ES2NeuralPost export names. ES2/UEVR are not runtime requirements.

To rebuild the full installer from the published binary package, extract it with `scripts/restore_package.ps1`, then use `scripts/build_installer.ps1 -PackageDirectory <extraction-folder>`. The manifest lists the exact engine and payload hashes. Game binaries, user sessions and logs are excluded.
