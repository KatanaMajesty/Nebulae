cmake_minimum_required(VERSION 3.21)

project(DXRNebulae LANGUAGES CXX)

add_executable(DXRNebulae WIN32)
set_property(TARGET DXRNebulae PROPERTY CXX_STANDARD 23)

target_sources(DXRNebulae PRIVATE
    "src/common/Defines.h"
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
    "src/nri/CommandAllocatorPool.cpp"
    "src/nri/CommandAllocatorPool.h"
    "src/nri/DepthStencilBuffer.cpp"
    "src/nri/DepthStencilBuffer.h"
    "src/nri/DescriptorAllocation.cpp"
    "src/nri/DescriptorAllocation.h"
    "src/nri/DescriptorHeap.cpp"
    "src/nri/DescriptorHeap.h"
    "src/nri/Device.cpp"
    "src/nri/Device.h"
    "src/nri/Material.h"
    "src/nri/Shader.cpp"
    "src/nri/Shader.h"
    "src/nri/ShaderCompiler.cpp"
    "src/nri/ShaderCompiler.h"
    "src/nri/StaticMesh.h"
    "src/nri/stdafx.h"
    "src/nri/Swapchain.cpp"
    "src/nri/Swapchain.h"
    "src/Nebulae.cpp"
    "src/Nebulae.h"
    "src/Raytracer.cpp"
    "src/Raytracer.h"
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

target_include_directories(DXRNebulae PRIVATE 
    "src"
    "vendor"
)

target_link_libraries(DXRNebulae
    "dxguid.lib"
    "d3d12.lib"
    "dxgi.lib"
    "dxcompiler.lib"
)

# Copy DLLs to the out path
# We need dxil.dll here to sign shaders compiled by DXC. Without signing them D3D12 runtime would fail
add_custom_command(TARGET DXRNebulae POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dxcompiler.dll ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dxil.dll $<TARGET_FILE_DIR:DXRNebulae>
  COMMAND_EXPAND_LISTS
)