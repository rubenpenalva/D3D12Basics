/// Copyright (c) 2016 Ruben Penalva Ambrona. All rights reserved.
/// Redistribution and use in source and binary forms, with or without modification, 
/// are permitted provided that the following conditions are met:
///    1. Redistributions of source code must retain the above copyright notice, this 
///    list of conditions and the following disclaimer.
///    2. Redistributions in binary form must reproduce the above copyright notice,
///    this list of conditions and the following disclaimer in the documentation and/or
///    other materials provided with the distribution.
///    3. The name of the author may not be used to endorse or promote products derived
///    from this software without specific prior written permission.
///
/// THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
/// INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
/// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, 
/// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED 
/// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
/// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
/// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF 
/// THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// app includes
#include "utils.h"
#include "d3d12gpus.h"
#include "d3d12resources.h"
#include "d3d12simplematerial.h"
#include "scene.h"

//c includes
#include <cassert>

// c++ includes
#include <chrono>
#include <sstream>

// windows includes
#include <windows.h>
#include <d3d12.h>

// thirdparty includes
#include "DirectXTK12/Inc/SimpleMath.h"

namespace
{
    // NOTE:  Assuming working directory contains the data folder
    const char* g_texture256FileName = "./data/texture_256.png";
    const char* g_texture1024FileName = "./data/texture_1024.jpg";

    const D3D12_VIEWPORT g_viewport =
    {
        0.0f, 0.0f,
        static_cast<float>(D3D12Basics::CustomWindow::GetResolution().m_width),
        static_cast<float>(D3D12Basics::CustomWindow::GetResolution().m_height),
        D3D12_MIN_DEPTH, D3D12_MAX_DEPTH
    };
    const RECT g_scissorRect =
    {
        0L, 0L,
        static_cast<long>(D3D12Basics::CustomWindow::GetResolution().m_width),
        static_cast<long>(D3D12Basics::CustomWindow::GetResolution().m_height)
    };

    struct D3D12MeshBuffer
    {
        D3D12Render::D3D12ResourceID m_vbID;
        D3D12Render::D3D12ResourceID m_ibID;
    };
    D3D12MeshBuffer AddMeshBufferLoadTasks(const D3D12Basics::Mesh& mesh, D3D12Render::D3D12GpuPtr gpu)
    {
        D3D12MeshBuffer meshBuffer;

        meshBuffer.m_vbID = D3D12Render::CreateD3D12Buffer(mesh.m_vertices.data(), mesh.VertexBufferSizeInBytes(),
                                                           (L"vb - " + mesh.m_name).c_str(), gpu);

        meshBuffer.m_ibID = D3D12Render::CreateD3D12Buffer(mesh.m_indices.data(), mesh.IndexBufferSizeInBytes(),
                                                           (L"ib - " + mesh.m_name).c_str(), gpu);
        return meshBuffer;
    }

    D3D12Render::D3D12GpuRenderTask CreateRenderTask(D3D12Render::D3D12GpuPtr gpu, const D3D12Basics::Mesh& mesh,
                                                     const D3D12MeshBuffer& meshBuffer,
                                                     D3D12Render::D3D12SimpleMaterialPtr simpleMaterial, 
                                                     D3D12Render::D3D12SimpleMaterialResources& resources)
    {
        D3D12Render::D3D12GpuRenderTask renderTask;

        renderTask.m_simpleMaterial = simpleMaterial;
        renderTask.m_simpleMaterialResources = resources;
        renderTask.m_viewport = g_viewport;
        renderTask.m_scissorRect = g_scissorRect;
        renderTask.m_vertexBufferResourceID = meshBuffer.m_vbID;
        renderTask.m_indexBufferResourceID = meshBuffer.m_ibID;
        renderTask.m_vertexCount = mesh.m_vertices.size();
        renderTask.m_vertexSize = D3D12Basics::Mesh::VertexSize;
        renderTask.m_indexCount = mesh.m_indices.size();
        
        return renderTask;
    }
}

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    D3D12Basics::CustomWindow customWindow;

    // Create main gpu
    D3D12Render::D3D12Gpus gpus;
    assert(gpus.Count());
    const D3D12Render::D3D12Gpus::GpuID mainGpuID = 0;
    D3D12Render::D3D12GpuPtr gpu = gpus.CreateGpu(mainGpuID, customWindow.GetHWND());
    
    // Create scene cpu resources 
    D3D12Basics::Camera camera(D3D12Basics::Float3(0.0f, 0.0f, -5.0f));

    D3D12Basics::Mesh plane = D3D12Basics::CreatePlane();
    D3D12MeshBuffer planeBuffer = AddMeshBufferLoadTasks(plane, gpu);

    D3D12Basics::Mesh sphere = D3D12Basics::CreateSphere(20, 20);
    D3D12MeshBuffer sphereBuffer = AddMeshBufferLoadTasks(sphere, gpu);

    D3D12Basics::Mesh cube = D3D12Basics::CreateCube();
    D3D12MeshBuffer cubeBuffer = AddMeshBufferLoadTasks(cube, gpu);

    const D3D12Render::D3D12ResourceID texture256ID = D3D12Render::CreateD3D12Texture(g_texture256FileName, L"texture2d - Texture 256", gpu);
    const D3D12Render::D3D12ResourceID texture1024ID = D3D12Render::CreateD3D12Texture(g_texture1024FileName, L"texture2d - Texture 1024", gpu);

    gpu->ExecuteCopyCommands();

    const D3D12Render::D3D12ResourceID planeCBID = gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4));
    const D3D12Render::D3D12ResourceID spheresCBs[2]
    {
        gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4)),
        gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4))
    };
    const D3D12Render::D3D12ResourceID cubesCBs[2]
    {
        gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4)),
        gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4))
    };

    // Create tasks for the gpu
    D3D12Render::D3D12GpuRenderTask clearRTRenderTask;
    clearRTRenderTask.m_simpleMaterial = nullptr;
    const float clearColor[4]{ 0.0f, 0.2f, 0.4f, 1.0f };
    memcpy(clearRTRenderTask.m_clearColor, clearColor, sizeof(float)*4);

    D3D12Render::D3D12SimpleMaterialPtr simpleMaterial = std::make_shared<D3D12Render::D3D12SimpleMaterial>(gpu->GetDevice());
    D3D12Render::D3D12SimpleMaterialResources sphereResources[2]
    {
        { spheresCBs[0], texture256ID },
        { spheresCBs[1], texture1024ID },
    };
    D3D12Render::D3D12SimpleMaterialResources planeResources;
    planeResources.m_cbID = planeCBID;
    planeResources.m_textureID = texture1024ID;
    D3D12Render::D3D12SimpleMaterialResources cubeResources[2]
    {
        { cubesCBs[0], texture256ID },
        { cubesCBs[1], texture1024ID },
    };

    D3D12Render::D3D12GpuRenderTask sphereRenderTasks[2]
    {
        CreateRenderTask(gpu, sphere, sphereBuffer, simpleMaterial, sphereResources[0]),
        CreateRenderTask(gpu, sphere, sphereBuffer, simpleMaterial, sphereResources[1])
    };
    D3D12Render::D3D12GpuRenderTask planeRenderTask = CreateRenderTask(gpu, plane, planeBuffer, simpleMaterial, planeResources);
    D3D12Render::D3D12GpuRenderTask cubeRenderTasks[2]
    {
        CreateRenderTask(gpu, cube, cubeBuffer, simpleMaterial, cubeResources[0]),
        CreateRenderTask(gpu, cube, cubeBuffer, simpleMaterial, cubeResources[1])
    };

    float lastDelta = 0.0f;
    float accumulatedTime = 0.0f;

    // Main loop
    while (1)
    {
        MSG msg = {};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                break;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            const auto frameBeginTick = std::chrono::high_resolution_clock::now();

            // Update scene
            {
                const float longitude = (1.0f / D3D12Basics::M_2PI) * accumulatedTime;
                const float latitude = D3D12Basics::M_PI_4 + D3D12Basics::M_PI_8;
                const float altitude = 5.0f;
                D3D12Basics::Float3 cameraPos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude);

                camera.TranslateLookingAt(cameraPos, D3D12Basics::Float3::Zero);
            }

            // Update gpu resources
            {
                const D3D12Basics::Matrix44 worldToClip = camera.WorldToCamera() * camera.CameraToClip();
                {
                    D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(D3D12Basics::Float3(2.0f, 0.5f, 0.0f));
                    const D3D12Basics::Matrix44 localToClip = (localToWorld * worldToClip).Transpose();
                    gpu->UpdateDynamicConstantBuffer(spheresCBs[0], &localToClip);
                }
                {
                    D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(D3D12Basics::Float3(-2.0f, 0.5f, 0.0f));
                    const D3D12Basics::Matrix44 localToClip = (localToWorld * worldToClip).Transpose();
                    gpu->UpdateDynamicConstantBuffer(spheresCBs[1], &localToClip);
                }
                {
                    D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(10.0f) * D3D12Basics::Matrix44::CreateRotationX(D3D12Basics::M_PI_2);
                    const D3D12Basics::Matrix44 localToClip = (localToWorld * worldToClip).Transpose();
                    gpu->UpdateDynamicConstantBuffer(planeCBID, &localToClip);
                }
                {
                    D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(D3D12Basics::Float3(0.0f, 0.5f, 2.0f));
                    const D3D12Basics::Matrix44 localToClip = (localToWorld * worldToClip).Transpose();
                    gpu->UpdateDynamicConstantBuffer(cubesCBs[0], &localToClip);
                }
                {
                    D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(D3D12Basics::Float3(0.0f, 0.5f, -2.0f));
                    const D3D12Basics::Matrix44 localToClip = (localToWorld * worldToClip).Transpose();
                    gpu->UpdateDynamicConstantBuffer(cubesCBs[1], &localToClip);
                }
            }

            // Record the command list
            {
                gpu->AddRenderTask(clearRTRenderTask);
                for (auto& renderTask : sphereRenderTasks)
                    gpu->AddRenderTask(renderTask);
                
                for (auto& renderTask : cubeRenderTasks)
                    gpu->AddRenderTask(renderTask);

                gpu->AddRenderTask(planeRenderTask);
            }

            // Execute command list
            {
                gpu->ExecuteGraphicsCommands();
            }

            // Present
            {
                gpu->Flush();
            }

            const auto frameEndTick = std::chrono::high_resolution_clock::now();

            lastDelta = std::chrono::duration<float>(frameEndTick - frameBeginTick).count();
            accumulatedTime += lastDelta;
        }
    }

    return 0;
}