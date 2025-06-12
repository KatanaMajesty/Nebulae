#include "DeferredRenderer.h"

#include "Nebulae.h" // TODO: Needed for assets directory, should be removed
#include "common/Log.h"
#include "core/Math.h"
#include "nri/Device.h"
#include "nri/DescriptorHeap.h"
#include "nri/ShaderCompiler.h"
#include "nri/imgui/UiContext.h"
#include "util/Memory.h"
#include "input/InputManager.h"

#include "DXRHelper/nv_helpers_dx12/RaytracingPipelineGenerator.h"

#include <format>
#include <array>
#include <vector>

// Helper to compute aligned buffer sizes
#define ROUND_UP(v, powerOf2Alignment) (((v) + (powerOf2Alignment)-1) & ~((powerOf2Alignment)-1))

namespace Neb
{

    bool DeferredRenderer::Init(UINT width, UINT height, nri::Swapchain* swapchain)
    {   
        m_width = width;
        m_height = height;
        m_swapchain = swapchain;

        InitGbuffers();
        InitGbufferHeaps();
        InitGbufferDepthStencilBuffer();
        InitGbufferDepthStencilSrv();
        InitGbufferShadersAndRootSignatures();
        InitGbufferPipelineState();
        InitGbufferInstanceCb();

        InitPBRConstantBuffers();
        InitPBRShadersAndRootSignature();
        InitPBRPipeline();

        InitHDRTonemapShadersAndRootSignature();
        InitHDRTonemapPipeline(m_swapchain->GetFormat());

        InitPathtracerShadersAndRootSignatures();
        InitPathtracerPipeline();
        InitPathtracerSBT();
        InitPathtracerConstantBuffers();
        InitPathtracerNRCQueryDebugResources(width, height);

        InitRadianceResolveShadersAndPSO();

        nri::ThrowIfFalse(m_svgfDenoiser.Init(width, height));
        return true;
    }

    void DeferredRenderer::Resize(UINT width, UINT height)
    {
        // avoid reallocating everything for no reason
        if (width == m_width && height == m_height)
            return;

        m_width = width;
        m_height = height;

        nri::ThrowIfFalse(m_svgfDenoiser.Resize(width, height));
        InitGbuffers();
        InitGbufferHeaps();
        InitGbufferDepthStencilSrv();

        InitPathtracerNRCQueryDebugResources(width, height);

        if (nri::NvRtxgiNRCIntegration::Get()->IsInitialised())
        {
            InitRadianceResolveShadersAndPSO();
            InitRadianceResolveCreateResourcesAndDescriptors();
        }

    }

    void DeferredRenderer::OnKeyInteraction(const KeyboardEvent_KeyInteraction& event)
    {
        if (event.Keycode == eKeycode_F1 && event.NextState == eKeycodeState_Pressed)
            m_showUI = !m_showUI;
    }

    void DeferredRenderer::BeginFrame(const RenderInfo& info)
    {
        m_frameIndex = info.frameIndex;
        m_renderInfo = info;

        if (m_scene != info.scene)
        {
            // scene update
            m_scene = info.scene;
            m_needsASUpdate = true;
            InitPathtracerScene(info.scene);
        }

        nrc::ContextSettings nrcContextSettings = nrc::ContextSettings();
        nrcContextSettings.learnIrradiance = true;
        nrcContextSettings.includeDirectLighting = false;
        nrcContextSettings.requestReset = false;
        nrcContextSettings.frameDimensions = { m_width, m_height };

        nrcContextSettings.trainingDimensions = nrc::ComputeIdealTrainingDimensions(nrcContextSettings.frameDimensions, 4);
        nrcContextSettings.maxPathVertices = m_globalIlluminationUI.nrcMaxPathVertices;
        nrcContextSettings.samplesPerPixel = m_globalIlluminationUI.giSamplesPerPixel;

        Vec3 min = info.scene->SceneBox.min;
        Vec3 max = info.scene->SceneBox.max;
        nrcContextSettings.sceneBoundsMin = { min.x, min.y, min.z };
        nrcContextSettings.sceneBoundsMax = { max.x, max.y, max.z };
        nrcContextSettings.smallestResolvableFeatureSize = 0.01f;
        if (ConfigureNRCState(nrcContextSettings))
        {
            InitPathtracerDescriptors();
            InitRadianceResolveCreateResourcesAndDescriptors();
        }

        // RTXGI NRC
        {
            nri::NvRtxgiNRCIntegration::Get()->BeginFrame(info.commandList, m_nrcFrameSettings);
            nri::NvRtxgiNRCIntegration::Get()->PopulateShaderConstants(&GetNrcConstants()); // update NrcConstants struct to then be used in constant buffer
        }

        m_svgfDenoiser.BeginFrame(info.frameIndex);

        // Update camera
        Vec3 currEyePos = m_scene->Camera.GetEyePos();
        if (currEyePos != m_eyePos)
        {
            m_eyePos = currEyePos;
            m_view = m_scene->Camera.UpdateLookAt();
            m_dynamicSceneThisFrame = true;
        }
        else if (m_dynamicSceneThisFrame)
        {
            // Camera was moving and now finally stopped - reset history and start denoising
            m_dynamicSceneThisFrame = false;
            m_resetHistory = true;
        }
        const float aspectRatio = m_width / static_cast<float>(m_height);
        m_proj = Mat4::CreatePerspectiveFieldOfView(ToRadians(60.0f), aspectRatio, 0.1f, 100.0f);
    }

    void DeferredRenderer::EndFrame()
    {
        m_svgfDenoiser.EndFrame();

        // Explicitly end RTXGI frame context
        nri::NvRtxgiNRCIntegration::Get()->EndFrame(nri::NRIDevice::Get().GetCommandQueue(nri::eCommandContextType_Graphics));
    }

    void DeferredRenderer::SubmitUICommands()
    {
        if (!m_showUI)
            return;
        
        ImGui::Begin("Sun settings");
        m_dynamicSceneThisFrame |= ImGui::SliderFloat("Sun diameter", &m_sceneSunUI.roughDiameter, 0.0f, 8.0f);
        m_dynamicSceneThisFrame |= ImGui::DragFloat3("Sun direction", &m_sceneSunUI.direction.x, 0.02f, -1.0f, 1.0f);
        m_dynamicSceneThisFrame |= ImGui::SliderFloat3("Sun radiance", &m_sceneSunUI.radiance.x, 0.0f, 32.0f);
        ImGui::End();

        ImGui::Begin("GI & Pathtracing");
        {
            if (ImGui::Button("Reload GI shaders"))
            {
                InitPathtracerShadersAndRootSignatures();
                InitPathtracerPipeline();
                InitPathtracerSBT();
            }

            ImGui::SliderInt("Samples per pixel", &m_globalIlluminationUI.giSamplesPerPixel, 1, 16);
            ImGui::SliderInt("Max path vertices", &m_globalIlluminationUI.nrcMaxPathVertices, 1, 32);
            ImGui::SliderFloat3("Sky color", &m_globalIlluminationUI.skyColor.x, 0.0f, 8.0f);
            ImGui::SliderFloat("Throughput threshold", &m_globalIlluminationUI.throughputThreshold, 0.0f, 1.0f);
        }
        ImGui::End();

        ImGui::Begin("Neural radiance cache (RTXGI)");
        {
            // NRC works better when the radiance values it sees internally are in a 'friendly'
            // range for it.
            // Applications often have quite different scales for their radiance units, so
            // we need to be able to scale these units in order to get that nice NRC-friendly range.
            // This value should broadly correspond to the average radiance that you might see
            // in one of your bright scenes (e.g. outdoors in daylight).
            ImGui::DragFloat("Max average radiance", &m_nrcFrameSettings.maxExpectedAverageRadianceValue, 0.025f, 0.0f, 32.0f);
            // This will prevent NRC from terminating on mirrors - it continue to the next vertex
            ImGui::Checkbox("Skip delta vertices", &m_nrcFrameSettings.skipDeltaVertices);
            // Knob for the termination heuristic to determine when it terminates the path.
            // The default value should give good quality.  You can decrease the value to
            // bias the algorithm to terminating earlier, trading off quality for performance.
            ImGui::DragFloat("Termination heuristic threshold", &m_nrcFrameSettings.terminationHeuristicThreshold, 0.01f, 0.0f, 1.0f);

            ImGui::Combo("Resolve Mode", (int*)&m_nrcFrameSettings.resolveMode, nrc::GetImGuiResolveModeComboString());

            // Debugging aid. You can disable the training to 'freeze' the state of the cache.
            ImGui::Checkbox("Train NRC", &m_nrcFrameSettings.trainTheCache);

            // Proportion of unbiased paths that we should do self-training on
            float proportionUnbiasedToSelfTrain = 1.0f;
            ImGui::DragFloat("Proportion unbiased (self-train)", &m_nrcFrameSettings.proportionUnbiasedToSelfTrain, 0.01f, 0.0f, 1.0f);

            // Proportion of training paths that should be 'unbiased'
            float proportionUnbiased = 0.0625f;
            ImGui::DragFloat("Proportion unbiased", &m_nrcFrameSettings.proportionUnbiased, 0.01f, 0.0f, 1.0f);

            // Allows the radiance from self-training to be attenuated or completely disabled
            // to help debugging.
            // If there's an error in the path tracer that breaks energy conservation for example,
            // the self training feedback can sometimes lead to the cache getting brighter
            // and brighter. This control can help debug such issues.
            float selfTrainingAttenuation = 1.0f;
            ImGui::DragFloat("Self-train attenuation", &m_nrcFrameSettings.selfTrainingAttenuation, 0.025f, 0.0f, 1.0f);

            // This controls how many training iterations are performed each frame,
            // which in turn determines the ideal number of training records that
            // the training/update path tracing pass is expected to generate.
            // Each training batch contains 16K training records derived from path segments
            // in the the NRC update path tracing pass.
            // `ComputeIdealTrainingDimensions` to set a lower resolution in
            // `FrameSettings::usedTrainingDimensions` (and your training/update dispatch).
            ImGui::SliderInt("Training iterations", (int*)&m_nrcFrameSettings.numTrainingIterations, 1, 32);
            ImGui::DragFloat("LR", &m_nrcFrameSettings.learningRate, 0.001f, 0.0f, 0.1f);

            if (ImGui::Button("Reset NRC"))
            {
                nrc::ContextSettings settings = m_nrcContextSettings;
                settings.requestReset = true;
                ConfigureNRCState(settings);
            }
        }
        ImGui::End();
    }

    void DeferredRenderer::SubmitCommandsGbuffer()
    {
        const RenderInfo& info = m_renderInfo;
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        nri::NRIDevice& device = nri::NRIDevice::Get();
        UINT width = m_width;
        UINT height = m_height;

        // Rendering context
        ID3D12GraphicsCommandList4* commandList = info.commandList;
        {
            SetupDescriptorHeaps(commandList);

            TransitionGbuffers(commandList,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
            {
                SetupGbufferRtvs(commandList);
                SetupViewports(commandList);
            }

            // Setup PSO
            commandList->SetGraphicsRootSignature(m_gbufferRS.GetD3D12RootSignature());
            commandList->SetGraphicsRootConstantBufferView(DEFERRED_RENDERER_ROOTS_INSTANCE_INFO, m_cbInstance.GetGpuVirtualAddress(info.backbufferIndex));
            commandList->SetPipelineState(m_pipelineState.Get());
            commandList->OMSetStencilRef(0xff);

            Mat4 viewProj = m_view * m_proj;

            // Now finally submit commands per-mesh
            for (nri::StaticMesh& staticMesh : info.scene->StaticMeshes)
            {
                const size_t numSubmeshes = staticMesh.Submeshes.size();
                NEB_ASSERT(numSubmeshes == staticMesh.SubmeshMaterials.size(),
                    "Static mesh is invalid. It has {} submeshes while only {} materials",
                    numSubmeshes, staticMesh.SubmeshMaterials.size());

                for (size_t i = 0; i < numSubmeshes; ++i)
                {
                    nri::StaticSubmesh& submesh = staticMesh.Submeshes[i];
                    nri::Material& material = staticMesh.SubmeshMaterials[i];

                    CbInstanceInfo cbInstanceInfo = CbInstanceInfo{
                        .InstanceToWorld = staticMesh.InstanceToWorld,
                        .ViewProj = viewProj,
                        .MaterialFlags = material.Flags // TODO: Would be nice to have a separate constant buffer for material properties
                    };
                    std::memcpy(m_cbInstance.GetMapping<CbInstanceInfo>(info.backbufferIndex), &cbInstanceInfo, sizeof(CbInstanceInfo));

                    commandList->SetGraphicsRootDescriptorTable(DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES, material.SrvRange.GpuAddress);

                    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                    commandList->IASetVertexBuffers(0, nri::eAttributeType_NumTypes, submesh.AttributeViews.data());
                    commandList->IASetIndexBuffer(&submesh.IBView);
                    commandList->DrawIndexedInstanced(submesh.NumIndices, 1, 0, 0, 0);
                }
            }

            // transition every gbuffer from render state to common
            TransitionGbuffers(commandList,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_COMMON,
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                D3D12_RESOURCE_STATE_COMMON);
        }
    }

    void DeferredRenderer::SubmitCommandsPBRLighting()
    {
        const RenderInfo& info = m_renderInfo;
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        nri::NRIDevice& device = nri::NRIDevice::Get();
        UINT width = m_width;
        UINT height = m_height;

        ID3D12GraphicsCommandList4* commandList = info.commandList;
        {
            // Check for AS update, and if needed - update
            InitRTAccelerationStructures(commandList);

            SetupDescriptorHeaps(commandList);
            SetupViewports(commandList);

            ID3D12Resource* resultBuffer = GetRadianceOutput();
            {
                std::array barriers = { CD3DX12_RESOURCE_BARRIER::Transition(resultBuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS) };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

                TransitionGbuffers(commandList,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }

            commandList->SetComputeRootSignature(m_pbrRS.GetD3D12RootSignature());
            commandList->SetComputeRootConstantBufferView(PBR_ROOT_CB_VIEW_DATA, m_cbViewData.GetGpuVirtualAddress(info.backbufferIndex));
            commandList->SetComputeRootConstantBufferView(PBR_ROOT_CB_LIGHT_ENV, m_cbLightEnv.GetGpuVirtualAddress(info.backbufferIndex));
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_GBUFFERS, m_gbufferSrvHeap.GpuAddress);
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_GBUFFER_NORMALS, m_svgfDenoiser.GetNormalSrv(m_svgfDenoiser.GetCurrentResourceIndex()) );
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_SCENE_DEPTH, m_svgfDenoiser.GetDepthSrv(m_svgfDenoiser.GetCurrentResourceIndex()) );
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_SCENE_STENCIL, m_svgfDenoiser.GetStencilSrv(m_svgfDenoiser.GetCurrentResourceIndex()) );
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_SCENE_TLAS_SRV, m_tlasSrvHeap.GpuAddress);
            commandList->SetComputeRootDescriptorTable(PBR_ROOT_HDR_OUTPUT_UAV, m_svgfDenoiser.GetCurrentRadianceUav());
            commandList->SetPipelineState(m_pbrPipeline.Get());

            CbViewData viewData = {
                .viewInv = m_view.Invert(),
                .projInv = m_proj.Invert(),
            };
            std::memcpy(m_cbViewData.GetMapping(info.backbufferIndex), &viewData, sizeof(CbViewData));

            CbLightEnvironment lightEnv = {
                .direction = m_sceneSunUI.direction,
                .tanHalfAngle = tanf(ToRadians(m_sceneSunUI.roughDiameter * 0.5f)),
                .radiance = m_sceneSunUI.radiance,
                .frameIndex = m_frameIndex,
            };
            std::memcpy(m_cbLightEnv.GetMapping(info.backbufferIndex), &lightEnv, sizeof(CbLightEnvironment));

            commandList->Dispatch((width + 7) / 8, (height + 7) / 8, 1); 
            {
                TransitionGbuffers(commandList,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    D3D12_RESOURCE_STATE_COMMON);

                auto pbrBarrier = CD3DX12_RESOURCE_BARRIER::Transition(resultBuffer, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
                commandList->ResourceBarrier(1, &pbrBarrier);
            }
        }
    }

    void DeferredRenderer::SubmitCommandsGIPathtrace()
    {
        const RenderInfo& info = m_renderInfo;
        NEB_ASSERT(info.scene, "Scene cannot be null");
        NEB_ASSERT(info.commandList, "Command list cannot be null");

        // Update CB data
        {
            // Nrc shader constants are updated in Renderer.cpp
            // TODO: maybe change that? Have NRC context here only?
            m_nrcConstantsCB.Upload(info.backbufferIndex, this->GetNrcConstants());
            m_globalConstantsCB.Upload(info.backbufferIndex, GlobalConstants{
                                                                 .frameIndex = info.frameIndex,
                                                                 .samplesPerPixel = uint32_t(m_globalIlluminationUI.giSamplesPerPixel),
                                                                 .nrcTrainingDownscale = Vec2(
                                                                     m_nrcConstants.frameDimensions.x / float(m_nrcConstants.trainingDimensions.x),
                                                                     m_nrcConstants.frameDimensions.y / float(m_nrcConstants.trainingDimensions.y)),
                                                                 .nrcMaxPathVertices = uint32_t(m_globalIlluminationUI.nrcMaxPathVertices),
                                                                 .cameraWorldPos = m_eyePos,
                                                                 .skyColor = m_globalIlluminationUI.skyColor,
                                                                 .sunLightDirection = m_sceneSunUI.direction,
                                                                 .sunLightRadiance = m_sceneSunUI.radiance,
                                                                 .sunTanHalfAngle = tanf(ToRadians(m_sceneSunUI.roughDiameter * 0.5f)),
                                                                 .throughputThreshold = m_globalIlluminationUI.throughputThreshold
                                                             });
        }
        
        ID3D12GraphicsCommandList4* commandList = info.commandList;

        std::vector<D3D12_RESOURCE_BARRIER> bindlessBarriers;
        for (nri::Rc<ID3D12Resource> resource : m_giScene.GetBindlessBuffers().resources)
        {
            bindlessBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        }
        commandList->ResourceBarrier(UINT(bindlessBarriers.size()), bindlessBarriers.data());

        // NRC QUERY PASS
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();
            UINT width = m_nrcConstants.frameDimensions.x;
            UINT height = m_nrcConstants.frameDimensions.x;
            {
                SetupDescriptorHeaps(commandList);

                commandList->SetPipelineState1(m_nrcQueryPSO.Get());
                commandList->SetComputeRootSignature(m_giGlobalRS.GetD3D12RootSignature());
                {
                    commandList->SetComputeRootConstantBufferView(PATHTRACER_ROOT_NRC_CONSTANTS, m_nrcConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));
                    commandList->SetComputeRootConstantBufferView(PATHTRACER_ROOT_GLOBAL_CONSTANTS, m_globalConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));

                    //// for these I need to add UAV creation in NvRtxgiNRC.cpp
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_NRC_BUFFERS, m_nrcBufferUavHeap.GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_NRC_NEBULAE_BUFFERS, m_NRCDebugBuffersHeap.GpuAddress);

                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_GBUFFER_TEXTURES, m_gbufferSrvHeap.GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_GBUFFER_NORMALS, m_svgfDenoiser.GetNormalSrv(m_svgfDenoiser.GetCurrentResourceIndex()));
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_DEPTH, m_svgfDenoiser.GetDepthSrv(m_svgfDenoiser.GetCurrentResourceIndex()));
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_STENCIL, m_svgfDenoiser.GetStencilSrv(m_svgfDenoiser.GetCurrentResourceIndex()));
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_BVH, m_tlasSrvHeap.GpuAddress);

                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_BINDLESS_TEXTURES, m_giScene.GetBindlessTextureHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_BINDLESS_BUFFERS, m_giScene.GetBindlessBufferHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_GEOMETRY_DATA, m_giScene.GetGeometryDataHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_MATERIAL_DATA, m_giScene.GetMaterialDataHeap().GpuAddress);
                }

                // Setup the raytracing task
                D3D12_DISPATCH_RAYS_DESC desc = {};
                // The ray generation shaders are always at the beginning of the SBT.
                // important to do in order to align with currentTableOffset
                uint32_t currentTableOffset = 0;
                D3D12_GPU_VIRTUAL_ADDRESS sbtAddress = m_rsQuerySBTBuffer->GetGPUVirtualAddress();
                desc.RayGenerationShaderRecord.StartAddress = sbtAddress;
                desc.RayGenerationShaderRecord.SizeInBytes = m_rsQuerySBTGenerator.GetRayGenSectionSize();
                currentTableOffset += ROUND_UP(desc.RayGenerationShaderRecord.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.MissShaderTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
                desc.MissShaderTable.StrideInBytes = m_rsQuerySBTGenerator.GetMissEntrySize();
                desc.MissShaderTable.SizeInBytes = m_rsQuerySBTGenerator.GetMissSectionSize();
                currentTableOffset += ROUND_UP(desc.MissShaderTable.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.HitGroupTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
                desc.HitGroupTable.StrideInBytes = m_rsQuerySBTGenerator.GetHitGroupEntrySize();
                desc.HitGroupTable.SizeInBytes = m_rsQuerySBTGenerator.GetHitGroupSectionSize();
                currentTableOffset += ROUND_UP(desc.HitGroupTable.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.Width = width;
                desc.Height = height;
                desc.Depth = 1;

                commandList->DispatchRays(&desc);
            }
        }

        // NRC UPDATE PASS
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();
            UINT width = m_nrcConstants.trainingDimensions.x;
            UINT height = m_nrcConstants.trainingDimensions.y;
            {
                SetupDescriptorHeaps(commandList);

                commandList->SetPipelineState1(m_nrcUpdatePSO.Get());
                commandList->SetComputeRootSignature(m_giGlobalRS.GetD3D12RootSignature());
                {
                    commandList->SetComputeRootConstantBufferView(PATHTRACER_ROOT_NRC_CONSTANTS, m_nrcConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));
                    commandList->SetComputeRootConstantBufferView(PATHTRACER_ROOT_GLOBAL_CONSTANTS, m_globalConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));

                    //// for these I need to add UAV creation in NvRtxgiNRC.cpp
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_NRC_BUFFERS, m_nrcBufferUavHeap.GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_NRC_NEBULAE_BUFFERS, m_NRCDebugBuffersHeap.GpuAddress);

                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_GBUFFER_TEXTURES, m_gbufferSrvHeap.GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_DEPTH, m_svgfDenoiser.GetDepthSrv(m_svgfDenoiser.GetCurrentResourceIndex()) );
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_STENCIL, m_svgfDenoiser.GetStencilSrv(m_svgfDenoiser.GetCurrentResourceIndex()) );
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_SCENE_BVH, m_tlasSrvHeap.GpuAddress);

                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_BINDLESS_TEXTURES, m_giScene.GetBindlessTextureHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_BINDLESS_BUFFERS, m_giScene.GetBindlessBufferHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_GEOMETRY_DATA, m_giScene.GetGeometryDataHeap().GpuAddress);
                    commandList->SetComputeRootDescriptorTable(PATHTRACER_ROOT_MATERIAL_DATA, m_giScene.GetMaterialDataHeap().GpuAddress);
                }

                // Setup the raytracing task
                D3D12_DISPATCH_RAYS_DESC desc = {};
                // The ray generation shaders are always at the beginning of the SBT.
                // important to do in order to align with currentTableOffset
                uint32_t currentTableOffset = 0;
                D3D12_GPU_VIRTUAL_ADDRESS sbtAddress = m_rsUpdateSBTBuffer->GetGPUVirtualAddress();
                desc.RayGenerationShaderRecord.StartAddress = sbtAddress;
                desc.RayGenerationShaderRecord.SizeInBytes = m_rsUpdateSBTGenerator.GetRayGenSectionSize();
                currentTableOffset += ROUND_UP(desc.RayGenerationShaderRecord.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.MissShaderTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
                desc.MissShaderTable.StrideInBytes = m_rsUpdateSBTGenerator.GetMissEntrySize();
                desc.MissShaderTable.SizeInBytes = m_rsUpdateSBTGenerator.GetMissSectionSize();
                currentTableOffset += ROUND_UP(desc.MissShaderTable.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.HitGroupTable.StartAddress = ROUND_UP(sbtAddress + currentTableOffset, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
                desc.HitGroupTable.StrideInBytes = m_rsUpdateSBTGenerator.GetHitGroupEntrySize();
                desc.HitGroupTable.SizeInBytes = m_rsUpdateSBTGenerator.GetHitGroupSectionSize();
                currentTableOffset += ROUND_UP(desc.HitGroupTable.SizeInBytes, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

                desc.Width = width;
                desc.Height = height;
                desc.Depth = 1;

                commandList->DispatchRays(&desc);
            }
        }

        for (D3D12_RESOURCE_BARRIER& barrier : bindlessBarriers)
        {
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        }
        commandList->ResourceBarrier(UINT(bindlessBarriers.size()), bindlessBarriers.data());

        {
            nri::NvRtxgiNRCIntegration::Get()->QueryAndTrain(commandList, nullptr);
        }

        {
            auto beforeBarriers = CD3DX12_RESOURCE_BARRIER::Transition(GetRadianceOutput(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            commandList->ResourceBarrier(1, &beforeBarriers);

#if 0
            SetupDescriptorHeaps(commandList);

            UINT width = m_width;
            UINT height = m_height;
            {
                commandList->SetPipelineState(m_radianceResolvePSO.Get());
                commandList->SetComputeRootSignature(m_radianceResolveRS.GetD3D12RootSignature());

                uint32_t resolution[2] = { width, height };
                commandList->SetComputeRootConstantBufferView(RADIANCE_RESOLVE_ROOT_NRC_CONSTANTS, m_nrcConstantsCB.GetGpuVirtualAddress(info.backbufferIndex));
                commandList->SetComputeRoot32BitConstants(RADIANCE_RESOLVE_ROOT_SCREEN_CONSTANTS, 2, resolution, 0);
                commandList->SetComputeRootDescriptorTable(RADIANCE_RESOLVE_ROOT_NRC_BUFFERS, m_radianceResolveNrcSrvHeap.GpuAddress);
                commandList->SetComputeRootDescriptorTable(RADIANCE_RESOLVE_ROOT_HDR_OUTPUT, m_pbrSrvUavHeap.GpuAt(HDR_UAV_INDEX));

                commandList->Dispatch(width / 8, height / 8, 1);
            }
#else
            nri::NvRtxgiNRCIntegration::Get()->Resolve(commandList, GetRadianceOutput());
#endif
            auto afterBarriers = CD3DX12_RESOURCE_BARRIER::Transition(GetRadianceOutput(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            commandList->ResourceBarrier(1, &afterBarriers);
        }
    }

    void DeferredRenderer::SubmitCommandsSVGFDenoising()
    {
        if (m_dynamicSceneThisFrame)
            return;

        ID3D12GraphicsCommandList4* commandList = m_renderInfo.commandList;

        // Pre-SVGF Barriers
        SVGFDenoiser& svgf = m_svgfDenoiser;
        SetupDescriptorHeaps(commandList); // update descriptor heaps after NRC
        {
            if (m_resetHistory)
            {
                m_resetHistory = false;
                svgf.ResetHistory(commandList);
            }

            std::array barriers = {
                //CD3DX12_RESOURCE_BARRIER::UAV(GetRadianceOutput() /*same as svgf.GetCurrentRadianceTexture()*/),
                // transition current radiance & history radiance
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetHistoryRadianceTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetCurrentRadianceTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                //  Depth & History depth will already be in SRV state from previous passes
                //  Normals will also be in SRV state as they are read in pathtracer beforehand
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetCurrentMomentsTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetHistoryMomentsTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetVarianceTexture(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
        // SVGF
        svgf.SubmitTemporalAccumulation(commandList);
        // Post-SVGF barriers, end of frame!
        {
            std::array barriers = {
                // transition everything back for now
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetHistoryRadianceTexture(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetCurrentRadianceTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetCurrentMomentsTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetHistoryMomentsTexture(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                CD3DX12_RESOURCE_BARRIER::Transition(svgf.GetVarianceTexture(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON),
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }
    }

    void DeferredRenderer::SubmitCommandsHDRTonemapping(ID3D12GraphicsCommandList4* commandList)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::Swapchain* swapchain = m_swapchain;
        UINT width = swapchain->GetWidth();
        UINT height = swapchain->GetHeight();

        {
            SetupDescriptorHeaps(commandList);
            
            D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, width, height);
            commandList->RSSetViewports(1, &viewport);

            D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, width, height);
            commandList->RSSetScissorRects(1, &scissorRect);

            ID3D12Resource* backbuffer = swapchain->GetCurrentBackbuffer();

            // Transition HDR input and backbuffer output
            std::array barriers = {
                CD3DX12_RESOURCE_BARRIER::Transition(this->GetRadianceOutput(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
                CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
            };
            commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

            D3D12_CPU_DESCRIPTOR_HANDLE backbufferRtv = swapchain->GetBackbufferRtvHandle(swapchain->GetCurrentBackbufferIndex());
            commandList->OMSetRenderTargets(1, &backbufferRtv, FALSE, nullptr);

            commandList->SetGraphicsRootSignature(m_tonemapRS.GetD3D12RootSignature());
            commandList->SetGraphicsRootDescriptorTable(TONEMAP_ROOT_HDR_INPUT, m_svgfDenoiser.GetCurrentRadianceSrv());
            commandList->SetPipelineState(m_tonemapPipeline.Get());

            // Fullscreen triangle
            commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            commandList->DrawInstanced(3, 1, 0, 0);
            {
                // Transition HDR input and backbuffer output
                std::array barriers = {
                    CD3DX12_RESOURCE_BARRIER::Transition(this->GetRadianceOutput(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
                    CD3DX12_RESOURCE_BARRIER::Transition(backbuffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON),
                };
                commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
            }
        }
    }

    void DeferredRenderer::TransitionGbuffers(ID3D12GraphicsCommandList4* commandList,
        D3D12_RESOURCE_STATES prev,
        D3D12_RESOURCE_STATES next,
        D3D12_RESOURCE_STATES depthPrev,
        D3D12_RESOURCE_STATES depthNext)
    {
        std::array barriers = {
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferAlbedo(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(m_svgfDenoiser.GetNormalArray(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferRoughnessMetalness(), prev, next),
            CD3DX12_RESOURCE_BARRIER::Transition(GetGbufferWorldPos(), prev, next),
            //CD3DX12_RESOURCE_BARRIER::Transition(m_depthStencilBuffer.GetBufferResource(), depthPrev, depthNext),
            CD3DX12_RESOURCE_BARRIER::Transition(m_svgfDenoiser.GetDepthArray(), depthPrev, depthNext),
        };
        commandList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }

    void DeferredRenderer::SetupDescriptorHeaps(ID3D12GraphicsCommandList4* commandList)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        
        std::array shaderVisibleHeaps = {
            device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).GetHeap(),
            device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).GetHeap(),
        };
        commandList->SetDescriptorHeaps(static_cast<UINT>(shaderVisibleHeaps.size()), shaderVisibleHeaps.data());
    }

    void DeferredRenderer::SetupGbufferRtvs(ID3D12GraphicsCommandList4* commandList)
    {
        // Clear gbuffers
        {
            static const Neb::Vec4 gbufferClearColor = Neb::Vec4(0.0f);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), &gbufferClearColor.x, 0, nullptr);
            commandList->ClearRenderTargetView(m_svgfDenoiser.GetNormalRtv(m_svgfDenoiser.GetCurrentResourceIndex()), &gbufferClearColor.x, 0, nullptr);
        }

        // set rtvs
        auto dsvDescriptor = m_svgfDenoiser.GetDepthDsv(m_svgfDenoiser.GetCurrentResourceIndex());
        std::array rtvDescriptors = {
            m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO),
            m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS),
            m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS),
            m_svgfDenoiser.GetNormalRtv(m_svgfDenoiser.GetCurrentResourceIndex()),
        };
        commandList->ClearDepthStencilView(dsvDescriptor, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        commandList->OMSetRenderTargets(
            4, rtvDescriptors.data(), FALSE, &dsvDescriptor);
    }

    void DeferredRenderer::SetupViewports(ID3D12GraphicsCommandList4* commandList)
    {
        D3D12_VIEWPORT viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, m_width, m_height);
        commandList->RSSetViewports(1, &viewport);

        D3D12_RECT scissorRect = CD3DX12_RECT(0, 0, m_width, m_height);
        commandList->RSSetScissorRects(1, &scissorRect);
    }

    void DeferredRenderer::InitGbuffers()
    {
        UINT width = m_width;
        UINT height = m_height;

        static constexpr auto CreateGbufferResource = [](
                                                          UINT width,
                                                          UINT height,
                                                          DXGI_FORMAT format) -> nri::Rc<D3D12MA::Allocation>
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();

            D3D12MA::Allocator* allocator = device.GetResourceAllocator();
            D3D12MA::ALLOCATION_DESC allocDesc = {
                .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
                .HeapType = D3D12_HEAP_TYPE_DEFAULT,
            };

            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(format, width, height, 1, 1);
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            Vec4 v = Vec4(0.0f);
            D3D12_CLEAR_VALUE clearValue = { .Format = format };
            std::memcpy(clearValue.Color, &v.x, sizeof(clearValue.Color));

            nri::Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(
                                   &allocDesc,
                                   &resourceDesc,
                                   D3D12_RESOURCE_STATE_COMMON, &clearValue,
                                   allocation.GetAddressOf(), nri::NullRIID, nullptr),
                "Failed to create gbuffer");
            return allocation;
        };

        m_gbufferAlbedo = CreateGbufferResource(width, height, DXGI_FORMAT_R11G11B10_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferAlbedo, "Albedo GBuffer DXGI_FORMAT_R11G11B10_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferAlbedo->GetResource(), "Albedo GBuffer DXGI_FORMAT_R11G11B10_FLOAT");

        //m_gbufferNormal = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        //NEB_SET_HANDLE_NAME(m_gbufferNormal, "Normal GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        //NEB_SET_HANDLE_NAME(m_gbufferNormal->GetResource(), "Normal GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");

        m_gbufferRoughnessMetalness = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferRoughnessMetalness, "Roughness/Metalness GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferRoughnessMetalness->GetResource(), "Roughness/Metalness GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");

        m_gbufferWorldPos = CreateGbufferResource(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
        NEB_SET_HANDLE_NAME(m_gbufferWorldPos, "World Position GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT allocation");
        NEB_SET_HANDLE_NAME(m_gbufferWorldPos->GetResource(), "World Position GBuffer DXGI_FORMAT_R16G16B16A16_FLOAT");
    }

    void DeferredRenderer::InitGbufferHeaps()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        if (m_gbufferSrvHeap.IsNull())
        {
            m_gbufferSrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(EGbufferSlot::GBUFFER_SLOT_NUM_SLOTS);
            NEB_ASSERT(!m_gbufferSrvHeap.IsNull(), "Failed to allocate SRV descriptors");
        }

        if (m_gbufferRtvHeap.IsNull())
        {
            m_gbufferRtvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).AllocateDescriptors(EGbufferSlot::GBUFFER_SLOT_NUM_SLOTS);
            NEB_ASSERT(!m_gbufferRtvHeap.IsNull(), "Failed to allocate RTV descriptors");
        }

        constexpr auto AllocateGbufferSrv = [](ID3D12Resource* resource,
                                                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                DXGI_FORMAT format,
                                                D3D12_SRV_DIMENSION dim = D3D12_SRV_DIMENSION_TEXTURE2D)
        {
            auto srvDesc = D3D12_SHADER_RESOURCE_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Texture2D = D3D12_TEX2D_SRV{ .MipLevels = 1 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateShaderResourceView(resource, &srvDesc, cpuHandle);
        };

        AllocateGbufferSrv(this->GetGbufferAlbedo(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), DXGI_FORMAT_R11G11B10_FLOAT);
        //AllocateGbufferSrv(this->GetGbufferNormal(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_NORMAL), DXGI_FORMAT_R16G16B16A16_FLOAT);
        AllocateGbufferSrv(this->GetGbufferRoughnessMetalness(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), DXGI_FORMAT_R16G16_FLOAT);
        AllocateGbufferSrv(this->GetGbufferWorldPos(), m_gbufferSrvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), DXGI_FORMAT_R16G16B16A16_FLOAT);

        constexpr auto AllocateGbufferRtv = [](ID3D12Resource* resource,
                                                D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                DXGI_FORMAT format,
                                                D3D12_RTV_DIMENSION dim = D3D12_RTV_DIMENSION_TEXTURE2D)
        {
            auto rtvDesc = D3D12_RENDER_TARGET_VIEW_DESC{
                .Format = format, // preserve
                .ViewDimension = dim,
                .Texture2D = D3D12_TEX2D_RTV{ .MipSlice = 0, .PlaneSlice = 0 }
            };
            nri::NRIDevice::Get().GetD3D12Device()->CreateRenderTargetView(resource, &rtvDesc, cpuHandle);
        };

        AllocateGbufferRtv(this->GetGbufferAlbedo(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ALBEDO), DXGI_FORMAT_R11G11B10_FLOAT);
        //AllocateGbufferRtv(this->GetGbufferNormal(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_NORMAL), DXGI_FORMAT_R16G16B16A16_FLOAT);
        AllocateGbufferRtv(this->GetGbufferRoughnessMetalness(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_ROUGHNESS_METALNESS), DXGI_FORMAT_R16G16_FLOAT);
        AllocateGbufferRtv(this->GetGbufferWorldPos(), m_gbufferRtvHeap.CpuAt(GBUFFER_SLOT_WORLD_POS), DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    void DeferredRenderer::InitGbufferDepthStencilBuffer()
    {
        UINT width = m_width;
        UINT height = m_height;
        //nri::ThrowIfFalse(m_depthStencilBuffer.Init(width, height, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        //    "Failed to create depth stencil buffer for deferred rendering");

        //NEB_SET_HANDLE_NAME(m_depthStencilBuffer.GetBufferResource(), "Deferred scene depth");
    }

    void DeferredRenderer::InitGbufferDepthStencilSrv()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        //m_depthStencilSrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(2);

        /*D3D12_SHADER_RESOURCE_VIEW_DESC depthDesc = {
            .Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{ .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 0 }
        };
        device.GetD3D12Device()->CreateShaderResourceView(m_depthStencilBuffer.GetBufferResource(), &depthDesc, m_depthStencilSrvHeap.CpuAt(0));

        D3D12_SHADER_RESOURCE_VIEW_DESC stencilDesc = {
            .Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT,
            .ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Texture2D = D3D12_TEX2D_SRV{ .MostDetailedMip = 0, .MipLevels = 1, .PlaneSlice = 1 }
        };
        device.GetD3D12Device()->CreateShaderResourceView(m_depthStencilBuffer.GetBufferResource(), &stencilDesc, m_depthStencilSrvHeap.CpuAt(1));*/
    }

    void DeferredRenderer::InitGbufferShadersAndRootSignatures()
    {
        nri::ShaderCompiler* compiler = nri::ShaderCompiler::Get();

        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::string shaderFilepath = (shaderDir / "deferred_gbuffers.hlsl").string();

        m_vsGbuffer = compiler->CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex),
            nri::eShaderCompilationFlag_None);

        m_psGbuffer = compiler->CompileShader(
            shaderFilepath,
            nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel),
            nri::eShaderCompilationFlag_None);

        m_gbufferRS = nri::RootSignature(DEFERRED_RENDERER_ROOTS_NUM_ROOTS, 1);
        m_gbufferRS.AddParamCbv(DEFERRED_RENDERER_ROOTS_INSTANCE_INFO, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
        m_gbufferRS.AddParamDescriptorTable(DEFERRED_RENDERER_ROOTS_MATERIAL_TEXTURES, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, nri::eMaterialTextureType_NumTypes, 0, 0), D3D12_SHADER_VISIBILITY_PIXEL);
        m_gbufferRS.AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));

        nri::ThrowIfFalse(m_gbufferRS.Init(&nri::NRIDevice::Get(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT));
    }

    void DeferredRenderer::InitGbufferPipelineState()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_gbufferRS.GetD3D12RootSignature();
        psoDesc.VS = m_vsGbuffer.GetBinaryBytecode();
        psoDesc.PS = m_psGbuffer.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.RasterizerState.FrontCounterClockwise = TRUE; // glTF 2.0 spec: the winding order triangle faces is CCW
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState.StencilEnable = true; // Enable stencil to obtain scene geometry data (for RT to fast 'miss')
        psoDesc.DepthStencilState.FrontFace = D3D12_DEPTH_STENCILOP_DESC{
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_REPLACE,
            .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS,
        };
        //psoDesc.DepthStencilState.BackFace = psoDesc.DepthStencilState.FrontFace; // Same for backfacing
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = D3D12_INPUT_LAYOUT_DESC{
            .pInputElementDescs = nri::StaticMeshInputLayout.data(),
            .NumElements = nri::StaticMeshInputLayout.size(),
        };
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = GBUFFER_SLOT_NUM_SLOTS + 1 /* + normals */;
        psoDesc.RTVFormats[GBUFFER_SLOT_ALBEDO] = DXGI_FORMAT_R11G11B10_FLOAT;
        psoDesc.RTVFormats[GBUFFER_SLOT_ROUGHNESS_METALNESS] = DXGI_FORMAT_R16G16_FLOAT;
        psoDesc.RTVFormats[GBUFFER_SLOT_WORLD_POS] = DXGI_FORMAT_R16G16B16A16_FLOAT;
        psoDesc.RTVFormats[3 /*(normals from SVGF denoiser)*/] = m_svgfDenoiser.GetNormalFormat();
        psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(device.GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_pipelineState.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitGbufferInstanceCb()
    {
        nri::ThrowIfFalse(m_cbInstance.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbInstanceInfo) }));
    }

    void DeferredRenderer::InitPBRShadersAndRootSignature()
    {
        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";

        m_csPBR = nri::ShaderCompiler::Get()->CompileShader(
            (shaderDir / "deferred_pbr.hlsl").string(),
            nri::ShaderCompilationDesc("CSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Compute));

        D3D12_DESCRIPTOR_RANGE1 gbufferSrvRange = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, GBUFFER_SLOT_NUM_SLOTS, 0, 1);
        D3D12_DESCRIPTOR_RANGE1 sceneDepthSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        D3D12_DESCRIPTOR_RANGE1 sceneStencilSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
        D3D12_DESCRIPTOR_RANGE1 sceneTlasSrv = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
        D3D12_DESCRIPTOR_RANGE1 hdrOutputUav = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
        m_pbrRS = nri::RootSignature(PBR_ROOT_NUM_ROOTS)
                      .AddParamCbv(PBR_ROOT_CB_VIEW_DATA, 0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC)
                      .AddParamCbv(PBR_ROOT_CB_LIGHT_ENV, 1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC)
                      .AddParamDescriptorTable(PBR_ROOT_GBUFFERS, std::array{ gbufferSrvRange })
                      .AddParamDescriptorTable(PBR_ROOT_GBUFFER_NORMALS, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, /*base_register*/ GBUFFER_SLOT_NUM_SLOTS, 1))
                      .AddParamDescriptorTable(PBR_ROOT_SCENE_DEPTH, std::array{ sceneDepthSrv })
                      .AddParamDescriptorTable(PBR_ROOT_SCENE_STENCIL, std::array{ sceneStencilSrv })
                      .AddParamDescriptorTable(PBR_ROOT_SCENE_TLAS_SRV, std::array{ sceneTlasSrv })
                      .AddParamDescriptorTable(PBR_ROOT_HDR_OUTPUT_UAV, std::array{ hdrOutputUav });
        nri::ThrowIfFalse(m_pbrRS.Init(&nri::NRIDevice::Get()));
    }

    void DeferredRenderer::InitPBRConstantBuffers()
    {
        nri::ThrowIfFalse(m_cbViewData.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbViewData) }));

        nri::ThrowIfFalse(m_cbLightEnv.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(CbLightEnvironment) }));
    }

    void DeferredRenderer::InitPBRPipeline()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_pbrRS.GetD3D12RootSignature();
        psoDesc.CS = m_csPBR.GetBinaryBytecode();
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(m_pbrPipeline.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitRTAccelerationStructures(ID3D12GraphicsCommandList4* commandList)
    {
        // Avoid rebuilding AS if scenes are same
        // TODO: this should not be just a pointer check really...
        if (m_blas.IsValid() && m_tlas.IsValid() && !m_needsASUpdate)
        {
            return;
        }

        Scene* scene = m_scene;

        NEB_LOG_INFO("Creating BLAS/TLAS structures...");

        NEB_ASSERT(!scene->StaticMeshes.empty());
        NEB_LOG_WARN_IF(scene->StaticMeshes.size() > 1, "Currently theres is only ray tracing support for a single static mesh in a scene");
        
        nri::StaticMesh& staticMesh = scene->StaticMeshes.front();
        m_blas = m_asBuilder.CreateBlas(commandList, m_asBuilder.QueryGeometryDescArray(staticMesh));

        nri::RTTopLevelInstance instance = {
            .blasAccelerationStructure = m_blas.accelerationStructureBuffer,
            .transformation = staticMesh.InstanceToWorld,
            .instanceID = 0,    // TODO: Figure it out
            .hitGroupIndex = 0, // TODO: Change to actually match SBT entry
            .flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE,
            //.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE,
        };

        m_tlas = m_asBuilder.CreateTlas(commandList, std::span(&instance, 1));
        NEB_LOG_INFO("Creating BLAS/TLAS structures - Done!");

        NEB_LOG_INFO("Creating TLAS SRV...");
        {
            nri::NRIDevice& device = nri::NRIDevice::Get();

            if (m_tlasSrvHeap.IsNull())
            {
                m_tlasSrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(1);
                NEB_LOG_INFO("Allocated descriptor heap chunk for TLAS");
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.RaytracingAccelerationStructure = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_SRV{
                .Location = m_tlas.accelerationStructureBuffer->GetGPUVirtualAddress()
            };
            device.GetD3D12Device()->CreateShaderResourceView(nullptr, &srvDesc, m_tlasSrvHeap.CpuAddress);
        }
        NEB_LOG_INFO("Creating TLAS SRV - Done!");
        m_needsASUpdate = false;
    }

    void DeferredRenderer::InitHDRTonemapShadersAndRootSignature()
    {
        const std::filesystem::path shaderDir = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";

        m_vsTonemap = nri::ShaderCompiler::Get()->CompileShader(
            (shaderDir / "fullscreen_triangle_vs.hlsl").string(), nri::ShaderCompilationDesc("VSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Vertex));

        m_psTonemap = nri::ShaderCompiler::Get()->CompileShader(
            (shaderDir / "tonemapping.hlsl").string(), nri::ShaderCompilationDesc("PSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Pixel));

        D3D12_DESCRIPTOR_RANGE1 tonemapHdrInput = CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
        m_tonemapRS = nri::RootSignature(TONEMAP_ROOT_NUM_ROOTS, 1)
                          .AddParamDescriptorTable(TONEMAP_ROOT_HDR_INPUT, std::array{ tonemapHdrInput }, D3D12_SHADER_VISIBILITY_PIXEL)
                          .AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));

        nri::ThrowIfFalse(m_tonemapRS.Init(&nri::NRIDevice::Get()));
    }

    void DeferredRenderer::InitHDRTonemapPipeline(DXGI_FORMAT outputFormat)
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_tonemapRS.GetD3D12RootSignature();
        psoDesc.VS = m_vsTonemap.GetBinaryBytecode();
        psoDesc.PS = m_psTonemap.GetBinaryBytecode();
        psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        psoDesc.SampleMask = UINT_MAX;
        psoDesc.InputLayout = D3D12_INPUT_LAYOUT_DESC();
        psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        psoDesc.NumRenderTargets = 1;
        psoDesc.RTVFormats[0] = outputFormat;
        psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
        psoDesc.SampleDesc = { 1, 0 };
        psoDesc.NodeMask = 0;
        psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(m_tonemapPipeline.ReleaseAndGetAddressOf())),
            "Failed to create tonemapping pipeline");
    }
    
    bool DeferredRenderer::ConfigureNRCState(const nrc::ContextSettings& nrcContextSettings)
    {
        if (m_nrcContextSettings != nrcContextSettings)
        {
            m_nrcContextSettings = nrcContextSettings;
            nri::NvRtxgiNRCIntegration::Get()->Configure(nrcContextSettings);
            return true;
        }
        return false;
    }

    void DeferredRenderer::InitPathtracerScene(Scene* scene)
    {
        nri::ThrowIfFalse(m_giScene.InitScene(scene->StaticMeshes), "Failed to initialize GI scene for pathtracer");
    }
    
    void DeferredRenderer::InitPathtracerDescriptors()
    {
        nri::NvRtxgiNRCIntegration* rtxgiIntegration = nri::NvRtxgiNRCIntegration::Get();
        nri::NRIDevice& device = nri::NRIDevice::Get();

        static constexpr auto GetUavDescOfNRCBuffer = [](const nri::NvRtxgiNRCIntegration::NRCBuffer& nrcBuffer)
        {
            NEB_ASSERT(nrcBuffer.info.allowUAV, "Uav is not allowed?");
            return D3D12_UNORDERED_ACCESS_VIEW_DESC{
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
                .Buffer = D3D12_BUFFER_UAV{
                    .FirstElement = 0,
                    .NumElements = static_cast<UINT>(nrcBuffer.info.elementCount),
                    .StructureByteStride = static_cast<UINT>(nrcBuffer.info.elementSize),
                }
            };
        };

        // order of UAVs is important
        m_nrcBufferUavHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(5);

        // only required NRC buffers, not all... IN ORDER declared in the shader!
        std::array nrcBuffers = {
            rtxgiIntegration->GetNRCBuffer(nrc::BufferIdx::QueryPathInfo),
            rtxgiIntegration->GetNRCBuffer(nrc::BufferIdx::TrainingPathInfo),
            rtxgiIntegration->GetNRCBuffer(nrc::BufferIdx::TrainingPathVertices),
            rtxgiIntegration->GetNRCBuffer(nrc::BufferIdx::QueryRadianceParams),
            rtxgiIntegration->GetNRCBuffer(nrc::BufferIdx::Counter),
        };
        for (uint32_t i = 0; i < nrcBuffers.size(); ++i)
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC desc = GetUavDescOfNRCBuffer(nrcBuffers[i]);
            device.GetD3D12Device()->CreateUnorderedAccessView(nrcBuffers[i].resource.Get(), nullptr, &desc, m_nrcBufferUavHeap.CpuAt(i));
        };
    }

    void DeferredRenderer::InitPathtracerShadersAndRootSignatures()
    {
        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderFilepath = shaderDirectory / "pathtracer.hlsl";

        m_rsUpdatePathtracer = nri::ShaderCompiler::Get()->CompileLibrary(
            shaderFilepath.string(),
            nri::LibraryCompilationDesc().AddDefine(nri::ShaderDefine("NRC_UPDATE", "1")),
            nri::eShaderCompilationFlag_Enable16BitTypes); // NRC needs 16-bit types to be enabled/supported

        m_rsQueryPathtracer = nri::ShaderCompiler::Get()->CompileLibrary(
            shaderFilepath.string(),
            nri::LibraryCompilationDesc().AddDefine(nri::ShaderDefine("NRC_QUERY", "1")),
            nri::eShaderCompilationFlag_Enable16BitTypes); // NRC needs 16-bit types to be enabled/supported

        NEB_ASSERT(m_rsUpdatePathtracer.HasBinary(), "Failed to compile update NRC pathtracer shader: {}", shaderFilepath.string());
        NEB_ASSERT(m_rsQueryPathtracer.HasBinary(), "Failed to compile query NRC pathtracer shader: {}", shaderFilepath.string());

        nri::NRIDevice& device = nri::NRIDevice::Get();
        {
            // Initialize root signatures for RT pathtracer
            // clang-format on
            nri::RootSignature& rs = m_giGlobalRS;
            rs = nri::RootSignature(PATHTRACER_ROOT_NUM_ROOTS, 1 /*1 static sampler for materials*/);
            rs.AddParamCbv(PATHTRACER_ROOT_NRC_CONSTANTS, /*register*/ 0, /*space*/ 0);
            rs.AddParamCbv(PATHTRACER_ROOT_GLOBAL_CONSTANTS, /*register*/ 1, /*space*/ 0);
            // NRC resources
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_NRC_BUFFERS, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 5, /*register*/ 0, /*space*/ 0));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_NRC_NEBULAE_BUFFERS, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ NRC_NEB_DEBUG_BUFFER_NUM_BUFFERS, /*register*/ 0, /*space*/ 1));
            // Gbuffers/additional scene information
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_GBUFFER_TEXTURES,  CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ GBUFFER_SLOT_NUM_SLOTS, /*register*/ 0, /*space*/ 1));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_GBUFFER_NORMALS,   CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ GBUFFER_SLOT_NUM_SLOTS, /*space*/ 1)); // normals separate
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_SCENE_DEPTH,       CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 0, /*space*/ 0));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_SCENE_STENCIL,     CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 1, /*space*/ 0));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_SCENE_BVH,         CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 2, /*space*/ 0));
            // bindless
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_BINDLESS_TEXTURES, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ UINT_MAX, /*register*/ 0, /*space*/ 3, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_BINDLESS_BUFFERS, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ UINT_MAX, /*register*/ 0, /*space*/ 4, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE));
            // mesh/material data for geometry reconstruction during ray tracing
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_GEOMETRY_DATA, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 2, /*space*/ 5));
            rs.AddParamDescriptorTable(PATHTRACER_ROOT_MATERIAL_DATA, CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 1, /*register*/ 3, /*space*/ 5));
            rs.AddStaticSampler(0, CD3DX12_STATIC_SAMPLER_DESC(0));
            // clang-format on
            nri::ThrowIfFalse(rs.Init(&device), "failed to init global rs for rt scene");
        }
        //m_giGlobalRS        = nri::RootSignature::Empty(&device);
        m_giRayGenRS        = nri::RootSignature::Empty(&device, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        m_giRayClosestHitRS = nri::RootSignature::Empty(&device, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
        m_giRayMissRS       = nri::RootSignature::Empty(&device, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);
    }

    void DeferredRenderer::InitPathtracerPipeline()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        struct PathtracerRayPayload
        {
            float hitDistance;
            uint32_t instanceID;
            uint32_t primitiveIndex;
            uint32_t geometryIndex;
            Vec2 barycentrics;
            uint32_t hitKind;
        };

        struct PathtracerRayAttributes
        {
            Vec2 uv;
        };

        // UPDATE PSO STATE
        nv_helpers_dx12::RayTracingPipelineGenerator updatePipelineGenerator(device.GetD3D12Device());
        {
            updatePipelineGenerator.AddLibrary(m_rsUpdatePathtracer.GetDxcBinaryBlob(),
                {
                    L"PathtracerRG",
                    L"PathtracerMS",
                    L"PathtracerCH",
                });

            updatePipelineGenerator.AddHitGroup(L"HitGroup", L"PathtracerCH");
            updatePipelineGenerator.AddRootSignatureAssociation(m_giRayGenRS.GetD3D12RootSignature(), { L"PathtracerRG" });
            updatePipelineGenerator.AddRootSignatureAssociation(m_giRayMissRS.GetD3D12RootSignature(), { L"PathtracerMS" });
            updatePipelineGenerator.AddRootSignatureAssociation(m_giRayClosestHitRS.GetD3D12RootSignature(), { L"HitGroup" });
            updatePipelineGenerator.SetMaxPayloadSize(sizeof(PathtracerRayPayload));      // see PathtracerRayPayload struct in pathtracer.hlsl
            updatePipelineGenerator.SetMaxAttributeSize(sizeof(PathtracerRayAttributes)); // see Attributes struct in pathtracer.hlsl
            updatePipelineGenerator.SetMaxRecursionDepth(MaxPathtracingRecursionDepth);
        }
        m_nrcUpdatePSO = updatePipelineGenerator.Generate(m_giGlobalRS.GetD3D12RootSignature());
        NEB_ASSERT(m_nrcUpdatePSO != NULL);

        // QUERY PSO STATE
        nv_helpers_dx12::RayTracingPipelineGenerator queryPipelineGenerator(device.GetD3D12Device());
        {
            queryPipelineGenerator.AddLibrary(m_rsQueryPathtracer.GetDxcBinaryBlob(),
                {
                    L"PathtracerRG",
                    L"PathtracerMS",
                    L"PathtracerCH",
                });

            queryPipelineGenerator.AddHitGroup(L"HitGroup", L"PathtracerCH");
            queryPipelineGenerator.AddRootSignatureAssociation(m_giRayGenRS.GetD3D12RootSignature(), { L"PathtracerRG" });
            queryPipelineGenerator.AddRootSignatureAssociation(m_giRayMissRS.GetD3D12RootSignature(), { L"PathtracerMS" });
            queryPipelineGenerator.AddRootSignatureAssociation(m_giRayClosestHitRS.GetD3D12RootSignature(), { L"HitGroup" });
            queryPipelineGenerator.SetMaxPayloadSize(sizeof(PathtracerRayPayload));      // see PathtracerRayPayload struct in pathtracer.hlsl
            queryPipelineGenerator.SetMaxAttributeSize(sizeof(PathtracerRayAttributes)); // see Attributes struct in pathtracer.hlsl
            queryPipelineGenerator.SetMaxRecursionDepth(MaxPathtracingRecursionDepth);
        }
        m_nrcQueryPSO = queryPipelineGenerator.Generate(m_giGlobalRS.GetD3D12RootSignature());
        NEB_ASSERT(m_nrcQueryPSO != NULL);
    }

    void DeferredRenderer::InitPathtracerSBT()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();

        static constexpr auto GenerateSBT = [](nv_helpers_dx12::ShaderBindingTableGenerator& generator, nri::Rc<ID3D12StateObject> pso) -> nri::Rc<ID3D12Resource>
        {
            // we only use global RS
            generator.Reset();
            generator.AddRayGenerationProgram(L"PathtracerRG", {});
            generator.AddMissProgram(L"PathtracerMS", {});
            generator.AddHitGroup(L"HitGroup", {});
            const uint32_t sbtSize = generator.ComputeSBTSize();

            D3D12MA::Allocator* allocator = nri::NRIDevice::Get().GetResourceAllocator();
            D3D12MA::ALLOCATION_DESC allocDesc = { .HeapType = D3D12_HEAP_TYPE_UPLOAD };

            nri::Rc<ID3D12Resource> sbtBuffer;
            // Create shader binding table buffer
            {
                D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sbtSize);

                // Ignoring InitialState D3D12_RESOURCE_STATE_UNORDERED_ACCESS.
                // Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
                nri::Rc<D3D12MA::Allocation> allocation;
                nri::ThrowIfFailed(allocator->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    allocation.GetAddressOf(),
                    IID_PPV_ARGS(sbtBuffer.ReleaseAndGetAddressOf())));
            }
            NEB_ASSERT(sbtBuffer, "Failed to create shader binding table buffer!");

            // Compile the SBT from the shader and parameters info
            try
            {
                nri::Rc<ID3D12StateObjectProperties> psoProperties;
                nri::ThrowIfFailed(pso->QueryInterface(psoProperties.GetAddressOf()));
                generator.Generate(sbtBuffer.Get(), psoProperties.Get());
            }
            catch (std::logic_error& error)
            {
                NEB_LOG_ERROR("Failed to generate Shader Binding Table: {}", error.what());
                NEB_ASSERT(false);
            }
            return sbtBuffer;
        };

        m_rsUpdateSBTBuffer = GenerateSBT(m_rsUpdateSBTGenerator, m_nrcUpdatePSO);
        m_rsQuerySBTBuffer = GenerateSBT(m_rsQuerySBTGenerator, m_nrcQueryPSO);
    }

    void DeferredRenderer::InitPathtracerConstantBuffers()
    {
        nri::ThrowIfFalse(m_nrcConstantsCB.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = AlignUp(sizeof(NrcConstants), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT) }));
        m_nrcConstantsCB.SetName("GI: NRC Constant buffer");

        nri::ThrowIfFalse(m_globalConstantsCB.Init(nri::ConstantBufferDesc{
            .NumBuffers = Renderer::NumInflightFrames,
            .NumBytesPerBuffer = sizeof(GlobalConstants) }));
        m_globalConstantsCB.SetName("GI: Global constant buffer");
    }

    void DeferredRenderer::InitPathtracerNRCQueryDebugResources(UINT width, UINT height)
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        m_NRCDebugBuffersHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(NRC_NEB_DEBUG_BUFFER_NUM_BUFFERS);

        D3D12MA::Allocator* allocator = device.GetResourceAllocator();
        D3D12MA::ALLOCATION_DESC allocDesc = {
            .Flags = D3D12MA::ALLOCATION_FLAG_COMMITTED,
            .HeapType = D3D12_HEAP_TYPE_DEFAULT,
        };
        {
            // NRC_NEB_DEBUG_BUFFER_QUERY_THROUGHPUT_MAP
            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, width, height);
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            nri::D3D12Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(
                &allocDesc,
                &resourceDesc,
                D3D12_RESOURCE_STATE_COMMON, // No need to use copy dest state. Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
                nullptr, allocation.GetAddressOf(),
                IID_PPV_ARGS(m_NRCDebugQueryThroughputMap.ReleaseAndGetAddressOf())));

            // hack to convert from string to wstring using <filesystem>. Not sure if that ok to do
            NEB_SET_HANDLE_NAME(m_NRCDebugQueryThroughputMap, "NRC-Debug QueryThroughputMap");


            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
                .Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D = D3D12_TEX2D_UAV()
            };
            device.GetD3D12Device()->CreateUnorderedAccessView(m_NRCDebugQueryThroughputMap.Get(), nullptr, &uavDesc, m_NRCDebugBuffersHeap.CpuAt(NRC_NEB_DEBUG_BUFFER_QUERY_THROUGHPUT_MAP));
        }
        
        {
            // NRC_NEB_DEBUG_BUFFER_QUERY_HIT_MAP
            D3D12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16_UINT, width, height);
            resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            nri::D3D12Rc<D3D12MA::Allocation> allocation;
            nri::ThrowIfFailed(allocator->CreateResource(
                &allocDesc,
                &resourceDesc,
                D3D12_RESOURCE_STATE_COMMON, // No need to use copy dest state. Buffers are effectively created in state D3D12_RESOURCE_STATE_COMMON.
                nullptr, allocation.GetAddressOf(),
                IID_PPV_ARGS(m_NRCDebugQueryHitMap.ReleaseAndGetAddressOf())));

            // hack to convert from string to wstring using <filesystem>. Not sure if that ok to do
            NEB_SET_HANDLE_NAME(m_NRCDebugQueryHitMap, "NRC-Debug HitMap");

            D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
                .Format = DXGI_FORMAT_R16_UINT,
                .ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D,
                .Texture2D = D3D12_TEX2D_UAV()
            };
            device.GetD3D12Device()->CreateUnorderedAccessView(m_NRCDebugQueryHitMap.Get(), nullptr, &uavDesc, m_NRCDebugBuffersHeap.CpuAt(NRC_NEB_DEBUG_BUFFER_QUERY_HIT_MAP));
        }
    }

    void DeferredRenderer::InitRadianceResolveShadersAndPSO()
    {
        const std::filesystem::path shaderDirectory = Nebulae::Get().GetSpecification().AssetsDirectory / "shaders";
        const std::filesystem::path shaderFilepath = shaderDirectory / "radiance_resolve.hlsl";

        m_csRadianceResolve = nri::ShaderCompiler::Get()->CompileShader(
            shaderFilepath.string(),
            nri::ShaderCompilationDesc("CSMain", nri::EShaderModel::sm_6_5, nri::EShaderType::Compute),
            nri::eShaderCompilationFlag_Enable16BitTypes); // NRC needs 16-bit types to be enabled/supported

        NEB_ASSERT(m_csRadianceResolve.HasBinary(), "Failed to compile radiance resolve compute shader: {}", shaderFilepath.string());

        nri::NRIDevice& device = nri::NRIDevice::Get();
        m_radianceResolveRS = nri::RootSignature(RADIANCE_RESOLVE_ROOT_NUM_ROOTS)
                                  .AddParamCbv(RADIANCE_RESOLVE_ROOT_NRC_CONSTANTS, 0)
                                  .AddParam32BitConstants(RADIANCE_RESOLVE_ROOT_SCREEN_CONSTANTS, 2, 1)
                                  .AddParamDescriptorTable(RADIANCE_RESOLVE_ROOT_NRC_BUFFERS,
                                      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, /*num_descriptors*/ 2, /*register*/ 0, /*space*/ 0))

                                  .AddParamDescriptorTable(RADIANCE_RESOLVE_ROOT_HDR_OUTPUT,
                                      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, /*num_descriptors*/ 1, /*register*/ 0, /*space*/ 1));

        nri::ThrowIfFalse(m_radianceResolveRS.Init(&device), "failed to init radiance resolve compute root sig");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_radianceResolveRS.GetD3D12RootSignature();
        psoDesc.CS = m_csRadianceResolve.GetBinaryBytecode();
        nri::ThrowIfFailed(nri::NRIDevice::Get().GetD3D12Device()->CreateComputePipelineState(
            &psoDesc, IID_PPV_ARGS(m_radianceResolvePSO.ReleaseAndGetAddressOf())));
    }

    void DeferredRenderer::InitRadianceResolveCreateResourcesAndDescriptors()
    {
        nri::NRIDevice& device = nri::NRIDevice::Get();
        nri::NvRtxgiNRCIntegration* nrcIntegration = nri::NvRtxgiNRCIntegration::Get();
        m_radianceResolveNrcSrvHeap = device.GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).AllocateDescriptors(2);
        {
            const nri::NvRtxgiNRCIntegration::NRCBuffer& buffer = nrcIntegration->GetNRCBuffer(nrc::BufferIdx::QueryPathInfo);
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer = D3D12_BUFFER_SRV{
                    .FirstElement = 0,
                    .NumElements = static_cast<UINT>(buffer.info.elementCount),
                    .StructureByteStride = static_cast<UINT>(buffer.info.elementSize),
                }
            };
            device.GetD3D12Device()->CreateShaderResourceView(buffer.resource.Get(), &srvDesc, m_radianceResolveNrcSrvHeap.CpuAt(0));
        }

        {
            // Query radiance buffer
            const nri::NvRtxgiNRCIntegration::NRCBuffer& buffer = nrcIntegration->GetNRCBuffer(nrc::BufferIdx::QueryRadiance);
            const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
                .Format = DXGI_FORMAT_UNKNOWN,
                .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
                .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
                .Buffer = D3D12_BUFFER_SRV{
                    .FirstElement = 0,
                    .NumElements = static_cast<UINT>(buffer.info.elementCount),
                    .StructureByteStride = static_cast<UINT>(buffer.info.elementSize),
                }
            };
            device.GetD3D12Device()->CreateShaderResourceView(buffer.resource.Get(), &srvDesc, m_radianceResolveNrcSrvHeap.CpuAt(1));
        }
        
    }

} // Neb namespace
