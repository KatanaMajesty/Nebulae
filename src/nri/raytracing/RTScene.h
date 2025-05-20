#pragma once

#include "RtCommon.h"

#include "core/Scene.h"
#include "nri/raytracing/RTAccelerationStructureBuilder.h"
#include "nri/stdafx.h"

#include <span>

namespace Neb::nri
{

    // we need both of these implementations for effective raytracing
    // GBuffer approach for getting attribute/material data:
    //      - easy to use
    //      - computationally cheap in comparison tho
    //      - cannot be used outside raygen (because ray hits may need the data that is absent in the screen space)
    //      - relying on the rasterization
    //
    // Bindless approach for getting attribute/material data:
    //      - a bit harder to use (because need to keep track of geometry and instance IDs by tlas-to-blas index map)
    //      - more memory expensive
    //      - can be used everywhere
    //      - not relying on the rast pipe

    // scene context for raytracing
    class RaytracedScene
    {
        // this would have a container of all used descriptors that are needed (that being normal map allocatinos, albedos, etc)
    public:
        // constructs invalid (empty) scene object
        RaytracedScene() = default;

        // creates raytraced scene context for specified scene, no actual scene initialization/rendering actually happens here
        RaytracedScene(Scene* scene)
            : m_scene(scene)
        {
        }

        // initializes data for the currently provided scene
        BOOL Init(ID3D12GraphicsCommandList4* commandList);

    private:
        Scene* m_scene = nullptr;

        RTAccelerationStructureBuilder m_asBuilder;
        RTTlasBuffers m_sceneTlas; // One large TLAS to store all BLASes
        std::vector<RTBlasBuffers> m_blasBuffers;
    };

}; // Neb::nri namespace