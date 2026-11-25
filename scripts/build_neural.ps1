param([string]$Runtime='C:\Users\Beren\Documents\KHARVOX')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
function BuildStep([string[]]$Arguments){& $cmake @Arguments; if($LASTEXITCODE){throw ('CMake failed: '+($Arguments -join ' '))}}
Push-Location $root
try{
    BuildStep @('-S','dependencies/SPIRV-Cross','-B','build/spirv-cross','-A','x64','-DSPIRV_CROSS_CLI=OFF','-DSPIRV_CROSS_ENABLE_TESTS=OFF')
    BuildStep @('--build','build/spirv-cross','--config','Release','--parallel','6')
    BuildStep @('-S','upstream/KHARVOX','-B','build/engine','-A','x64',
        '-DKHARVOX_BUILD_DLSS_PILOT=ON','-DKHARVOX_BUILD_SFS_COMPILER=ON',
        "-DKHARVOX_SPIRV_CROSS_SOURCE=$root/dependencies/SPIRV-Cross",
        "-DKHARVOX_SPIRV_CROSS_LIB=$root/build/spirv-cross/Release",
        "-DKHARVOX_GLSLANG_ROOT=$root/dependencies/glslang-install",
        "-DKHARVOX_NGX_SDK=$root/components/FOVEATED_DLSS/dependencies/SDK/NVIDIA-DLSS-310.9.1",
        "-DKHARVOX_DLSS_RUNTIME=$Runtime/DoomLayer/ngx",
        "-DKHARVOX_OPENXR_LOADER=$Runtime/openxr_loader.dll",
        "-DKHARVOX_FOVEATED_SOURCE=$root/components/FOVEATED_DLSS",
        "-DKHARVOX_DOOM_TEMPORAL_REPLAY=$root/fixtures/engine-replay.bin",
        "-DDLSS_FOVEADO_VULKAN_INCLUDE=$root/upstream/KHARVOX/third-party/vulkan-headers/include",
        "-DDLSS_FOVEADO_GLSLANG=$root/dependencies/glslang-install/bin/glslang.exe")
    BuildStep @('--build','build/engine','--config','Release','--target','KharvoxLayer','--parallel','6')
    BuildStep @('-S','development/neural-post','-B','build/neural-post','-A','x64')
    BuildStep @('--build','build/neural-post','--config','Release','--parallel','6')
}finally{Pop-Location}
