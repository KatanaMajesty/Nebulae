cmake_minimum_required(VERSION 3.21)

project(DXRNebulae LANGUAGES CXX)

option(NEBULAE_WIN32_APPLICATION "Build Nebulae executable as Win32 application" OFF)

if(NEBULAE_WIN32_APPLICATION)
    add_executable(DXRNebulae WIN32)
    target_compile_definitions(DXRNebulae PUBLIC NEB_WIN32_APPLICATION=1)
else()
    add_executable(DXRNebulae)
endif(NEBULAE_WIN32_APPLICATION)

set_property(TARGET DXRNebulae PROPERTY CXX_STANDARD 23)

target_compile_definitions(DXRNebulae
PUBLIC
    $<$<CONFIG:Debug>:NEB_DEBUG>
    $<$<CONFIG:Release>:NEB_RELEASE>
)

target_sources(DXRNebulae PRIVATE
    "src/common/Assert.h"
    "src/common/Log.cpp"
    "src/common/Log.h"
    "src/common/TimeWatch.h"

    "src/core/GLTFSceneImporter.cpp"
    "src/core/GLTFSceneImporter.h"
    "src/core/InspectCamera.h"
    "src/core/Math.h"
    "src/core/Scene.cpp"
    "src/core/Scene.h"

    "src/input/InputCallback.h"
    "src/input/InputManager.h"
    "src/input/Keyboard.cpp"
    "src/input/Keyboard.h"
    "src/input/Mouse.cpp"
    "src/input/Mouse.h"

    "src/nri/imgui/UiContext.cpp"
    "src/nri/imgui/UiContext.h"

    "src/nri/nvidia/NsightAftermathCrashTracker.cpp"
    "src/nri/nvidia/NsightAftermathCrashTracker.h"
    "src/nri/nvidia/NsightAftermathHelpers.h"
    "src/nri/nvidia/NvApi.cpp"
    "src/nri/nvidia/NvApi.h"
    "src/nri/nvidia/NvRtxgiNRC.cpp"
    "src/nri/nvidia/NvRtxgiNRC.h"

    "src/nri/raytracing/RTAccelerationStructureBuilder.cpp"
    "src/nri/raytracing/RTAccelerationStructureBuilder.h"
    "src/nri/raytracing/RTCommon.h"
    "src/nri/raytracing/RTDescriptorArray.h"
    #"src/nri/raytracing/RTPipeline.cpp"
    #"src/nri/raytracing/RTPipeline.h"
    "src/nri/raytracing/RTScene.cpp"
    "src/nri/raytracing/RTScene.h"

    "src/nri/CommandAllocatorPool.cpp"
    "src/nri/CommandAllocatorPool.h"
    "src/nri/ConstantBuffer.cpp"
    "src/nri/ConstantBuffer.h"
    "src/nri/DepthStencilBuffer.cpp"
    "src/nri/DepthStencilBuffer.h"
    "src/nri/DescriptorHeap.cpp"
    "src/nri/DescriptorHeap.h"
    "src/nri/DescriptorHeapAllocation.h"
    "src/nri/Device.cpp"
    "src/nri/Device.h"
    "src/nri/GIProcessedScene.cpp"
    "src/nri/GIProcessedScene.h"
    "src/nri/Material.h"
    "src/nri/PIXRuntime.h"
    "src/nri/RootSignature.cpp"
    "src/nri/RootSignature.h"
    "src/nri/Shader.cpp"
    "src/nri/Shader.h"
    "src/nri/ShaderCompiler.cpp"
    "src/nri/ShaderCompiler.h"
    "src/nri/StaticMesh.h"
    "src/nri/stdafx.h"
    "src/nri/Swapchain.cpp"
    "src/nri/Swapchain.h"

    "src/util/File.h"
    "src/util/Memory.h"
    "src/util/ScopedPointer.h"
    "src/util/Types.h"

    "src/ArgumentParser.h"
    "src/DeferredRenderer.cpp"
    "src/DeferredRenderer.h"
    "src/Nebulae.cpp"
    "src/Nebulae.h"
    "src/Raytracer.cpp"
    "src/Raytracer.h"
    "src/SVGFDenoiser.cpp"
    "src/SVGFDenoiser.h"
    "src/Renderer.cpp"
    "src/Renderer.h"
    "src/Win.h"
    "src/WinMain.cpp"
)

target_sources(DXRNebulae PRIVATE
    "vendor/D3D12MA/D3D12MemAlloc.cpp"
    "vendor/D3D12MA/D3D12MemAlloc.h"
)

target_sources(DXRNebulae PRIVATE
    "vendor/TinyGLTF/tiny_gltf.cc"
)

# Create an empty list variable
set(NEBULAE_DLL_LIST "")

# NVidia libs
set(NV_GFSDK_AFTERMATH_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/NVIDIA/Nsight_Aftermath")
set(NV_GFSDK_AFTERMATH_LIBRARY_DIR "${NV_GFSDK_AFTERMATH_DIR}/lib/x64")
target_sources(DXRNebulae PRIVATE
    "${NV_GFSDK_AFTERMATH_DIR}/include/GFSDK_Aftermath.h"
    "${NV_GFSDK_AFTERMATH_DIR}/include/GFSDK_Aftermath_Defines.h"
    "${NV_GFSDK_AFTERMATH_DIR}/include/GFSDK_Aftermath_GpuCrashDump.h"
    "${NV_GFSDK_AFTERMATH_DIR}/include/GFSDK_Aftermath_GpuCrashDumpDecoding.h"
)
target_link_directories(DXRNebulae PRIVATE ${NV_GFSDK_AFTERMATH_LIBRARY_DIR})
target_compile_definitions(DXRNebulae PRIVATE NEB_USE_NSIGHT_AFTERMATH)

set(NV_DXRHELPER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/DXRHelper")
target_sources(DXRNebulae PRIVATE
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/BottomLevelASGenerator.cpp"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/BottomLevelASGenerator.h"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/RaytracingPipelineGenerator.cpp"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/RaytracingPipelineGenerator.h"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/RootSignatureGenerator.cpp"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/RootSignatureGenerator.h"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/ShaderBindingTableGenerator.cpp"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/ShaderBindingTableGenerator.h"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/TopLevelASGenerator.cpp"
    "${NV_DXRHELPER_DIR}/nv_helpers_dx12/TopLevelASGenerator.h"
)

set(NV_RTXGI_NRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/NVIDIA/RTXGI/Nrc")
target_include_directories(DXRNebulae PRIVATE "${NV_RTXGI_NRC_DIR}/Include")
target_link_directories(DXRNebulae PRIVATE "${NV_RTXGI_NRC_DIR}/Lib")

set(NV_API_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/NVIDIA/NvApi")

# ImGui sources
set(IMGUI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/imgui")
add_library(    imgui)
target_sources( imgui PRIVATE
    # Dx12 backend
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_dx12.h"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_SOURCE_DIR}/backends/imgui_impl_win32.h"

    # common
    "${IMGUI_SOURCE_DIR}/imgui.cpp"
    "${IMGUI_SOURCE_DIR}/imgui.h"
    "${IMGUI_SOURCE_DIR}/imgui_demo.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_internal.h"
    "${IMGUI_SOURCE_DIR}/imgui_tables.cpp"
    "${IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${IMGUI_SOURCE_DIR}/imstb_rectpack.h"
    "${IMGUI_SOURCE_DIR}/imstb_textedit.h"
    "${IMGUI_SOURCE_DIR}/imstb_truetype.h"
)
# imgui header files are included relative to the imgui directory, thus add include dirs publicly (as nebulae also uses those headers)
# regardles, encourage to include headers relative to vendor dir
target_include_directories(imgui PUBLIC "${IMGUI_SOURCE_DIR}")

# PIX event runtime
set(PIX_LIBRARY_SOURCE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vendor/WinPixEventRuntime")
set(PIX_LIBRARY_DIR "${PIX_LIBRARY_SOURCE_PATH}/bin/x64")
target_include_directories(DXRNebulae PRIVATE "${PIX_LIBRARY_SOURCE_PATH}/Include")
target_link_directories(DXRNebulae PRIVATE ${PIX_LIBRARY_DIR})

target_include_directories(DXRNebulae PRIVATE 
    "src"
    "vendor"
)

target_link_libraries(DXRNebulae
    "dxguid.lib"
    "d3d12.lib"
    "dxgi.lib"
    "dxcompiler.lib"
    "GFSDK_Aftermath_Lib.x64.lib"
    "imgui"
    "${NV_API_DIR}/nvapi64.lib"
    "NRC_D3D12"
    "WinPixEventRuntime.lib"
)

# We need dxil.dll here to sign shaders compiled by DXC. Without signing them D3D12 runtime would fail
list(APPEND NEBULAE_DLL_LIST ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dxcompiler.dll)
list(APPEND NEBULAE_DLL_LIST ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dxil.dll)
list(APPEND NEBULAE_DLL_LIST ${NV_GFSDK_AFTERMATH_LIBRARY_DIR}/GFSDK_Aftermath_Lib.x64.dll)
# For RTXGI NRC
list(APPEND NEBULAE_DLL_LIST ${NV_RTXGI_NRC_DIR}/Bin/cudart64_12.dll)
list(APPEND NEBULAE_DLL_LIST ${NV_RTXGI_NRC_DIR}/Bin/nvrtc-builtins64_125.dll)
list(APPEND NEBULAE_DLL_LIST ${NV_RTXGI_NRC_DIR}/Bin/nvrtc64_120_0.dll)
list(APPEND NEBULAE_DLL_LIST ${NV_RTXGI_NRC_DIR}/Bin/NRC_D3D12.dll)
# PIX
list(APPEND NEBULAE_DLL_LIST ${PIX_LIBRARY_DIR}/WinPixEventRuntime.dll)
# Join the list elements with a space separator
string(REPLACE ";" " " my_list_str "${NEBULAE_DLL_LIST}")

add_custom_command(TARGET DXRNebulae POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${NEBULAE_DLL_LIST} $<TARGET_FILE_DIR:DXRNebulae>
  COMMAND_EXPAND_LISTS
)