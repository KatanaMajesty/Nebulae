cmake_minimum_required(VERSION 3.21)

project(DXRNebulae LANGUAGES CXX)

add_executable(DXRNebulae WIN32
    "src/Raytracing/NRIDescriptorHeap.cpp" 
    "src/Raytracing/NRIManager.cpp"
    "src/Raytracing/RayTracer.cpp"
    "src/Raytracing/Shader.cpp" 
    "src/Raytracing/ShaderCompiler.cpp"
    "src/WinMain.cpp"
    "vendor/D3D12MA/D3D12MemAlloc.cpp"
)

set_property(TARGET DXRNebulae PROPERTY CXX_STANDARD 23)

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