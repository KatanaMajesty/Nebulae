cmake_minimum_required(VERSION 3.21)

project(DXRNebulae LANGUAGES CXX)

add_executable(DXRNebulae WIN32)
set_property(TARGET DXRNebulae PROPERTY CXX_STANDARD 23)

target_sources(DXRNebulae PRIVATE
    "src/common/Defines.h"
    "src/nri/DescriptorHeap.cpp"
    "src/nri/DescriptorHeap.h"
    "src/nri/Manager.cpp"
    "src/nri/Manager.h"
    "src/nri/Shader.cpp"
    "src/nri/Shader.h"
    "src/nri/ShaderCompiler.cpp"
    "src/nri/ShaderCompiler.h"
    "src/nri/stdafx.h"
    "src/nri/Swapchain.cpp"
    "src/nri/Swapchain.h"
    "src/Nebulae.cpp"
    "src/Nebulae.h"
    "src/Win.h"
    "src/WinMain.cpp"
)

target_sources(DXRNebulae PRIVATE
    "vendor/D3D12MA/D3D12MemAlloc.cpp"
    "vendor/D3D12MA/D3D12MemAlloc.h"
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