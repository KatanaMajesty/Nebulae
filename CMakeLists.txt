cmake_minimum_required(VERSION 3.21)

project(DXRNebulae LANGUAGES CXX)

add_executable(DXRNebulae WIN32
    "src/Raytracing/RayTracer.cpp"
    "src/WinMain.cpp"
)

target_include_directories(DXRNebulae PRIVATE 
    "src"
    "vendor"
)