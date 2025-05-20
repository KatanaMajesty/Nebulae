#include "RtScene.h"

#include "common/Assert.h"

namespace Neb::nri
{

    BOOL RaytracedScene::Init(ID3D12GraphicsCommandList4* commandList)
    {
        NEB_ASSERT(m_scene, "No scene provided!");

        m_blasBuffers.clear();
        m_blasBuffers.resize(m_scene->StaticMeshes.size());

        // TODO: Remark -> right now we have 1 tlas instance per 1 blas ALWAYS
        // if this is to change, we need to properly push instances back (and rewrite half the thing actually)
        std::vector<RTTopLevelInstance> instances(m_scene->StaticMeshes.size());

        for (UINT i = 0; i < m_scene->StaticMeshes.size(); ++i)
        {
            StaticMesh& mesh = m_scene->StaticMeshes[i];
            std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryArray = m_asBuilder.QueryGeometryDescArray(mesh);

            // BLAS creation should happen per-mesh as every submesh inside the mesh will share the same
            // instanceToWorld transformation
            m_blasBuffers[i] = m_asBuilder.CreateBlas(commandList, geometryArray);
            ThrowIfFalse(m_blasBuffers[i].IsValid(), "Created blas buffers are not valid!");

            instances[i] = RTTopLevelInstance{
                .blasAccelerationStructure = m_blasBuffers[i].accelerationStructureBuffer,
                .transformation = mesh.InstanceToWorld,
                .instanceID = i,
                .hitGroupIndex = 0, // TODO: Change to actually match SBT entry
                .flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE,
            };
        }

        m_sceneTlas = m_asBuilder.CreateTlas(commandList, instances);
        return true;
    }

}; // Neb::nri namespace