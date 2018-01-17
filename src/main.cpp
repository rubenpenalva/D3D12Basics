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

//c includes
#include <cassert>

// c++ includes
#include <chrono>
#include <sstream>

// windows includes
#include <windows.h>
#include <d3d12.h>

// thirdparty includes
#include "DirectxMath/Inc/DirectXMath.h"

namespace
{
    using Float2    = DirectX::XMFLOAT2;
    using Float3    = DirectX::XMFLOAT3;
    using Matrix44  = DirectX::XMFLOAT4X4;

    const size_t g_vertexElemsCount = 5;
    const size_t g_verticesCount = 4;
    float g_vertices[g_verticesCount * g_vertexElemsCount]
    {
        //Position                                                                 UV
        0.5f, 0.5f*Utils::CustomWindow::GetResolution().m_aspectRatio, 0.0f,       1.0f, 0.0f,
        0.5f, -0.5f*Utils::CustomWindow::GetResolution().m_aspectRatio, 0.0f,      1.0f, 1.0f,
        -0.5f, 0.5f*Utils::CustomWindow::GetResolution().m_aspectRatio, 0.0f,      0.0f, 0.0f,
        -0.5f, -0.5f*Utils::CustomWindow::GetResolution().m_aspectRatio, 0.0f,     0.0f, 1.0f,
    };
    const size_t g_vertexSize = g_vertexElemsCount * sizeof(float);
    const size_t g_vertexBufferSize = g_verticesCount * g_vertexSize;

    const size_t g_indexCount = 6;
    uint16_t g_indices[g_indexCount] = { 0, 1, 2, 1, 3, 2 };
    const size_t g_indexBufferSize = sizeof(uint16_t) * g_indexCount;

    // NOTE:  Assuming working directory contains the data folder
    const char* g_texture256FileName = "./data/texture_256.png";
    const char* g_texture1024FileName = "./data/texture_1024.jpg";

    struct Mesh
    {
        std::vector<Float3>     m_vertexPositions;
        std::vector<Float2>     m_vertexUVs;

        std::vector<uint16_t>   m_indices;
    };

    Mesh CreatePlane()
    {
        Mesh planeMesh;

        planeMesh.m_vertexPositions = { { -0.5f, -0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f } };
        planeMesh.m_vertexUVs = { {0.0f, 1.0f}, { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };

        planeMesh.m_indices = { 0, 1, 2, 0, 2, 3 };

        return planeMesh;
    }

    struct Camera
    {
        // Camera matrix
        Matrix44 m_worldToCamera;

        // Projection matrix
        Matrix44 m_cameraToClip;
        
        Camera()
        {

        }
    };
}

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    Utils::CustomWindow customWindow;
    
    // Create main gpu
    D3D12Render::D3D12Gpus gpus;
    assert(gpus.Count());
    const D3D12Render::D3D12Gpus::GpuID mainGpuID = 0;
    D3D12Render::D3D12GpuPtr gpu = gpus.CreateGpu(mainGpuID, customWindow.GetHWND());
    
    // Load resources for the scene
    const D3D12Render::D3D12ResourceID vertexBufferResourceID = D3D12Render::CreateD3D12Buffer(g_vertices, g_vertexBufferSize, L"vb - Viewport Quad", gpu);
    const D3D12Render::D3D12ResourceID indexBufferResourceID = D3D12Render::CreateD3D12Buffer(g_indices, g_indexBufferSize, L"ib - Viewport Quad", gpu);
    const D3D12Render::D3D12ResourceID texture256ID = D3D12Render::CreateD3D12Texture(g_texture256FileName, L"texture2d - Texture 256", gpu);
    const D3D12Render::D3D12ResourceID dynamicConstantBufferID = gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4));
    gpu->ExecuteCopyCommands();

    // Create tasks for the gpu
    D3D12Render::D3D12GpuRenderTask clearRTRenderTask;
    clearRTRenderTask.m_simpleMaterial = nullptr;
    const float clearColor[4]{ 0.0f, 0.2f, 0.4f, 1.0f };
    memcpy(clearRTRenderTask.m_clearColor, clearColor, sizeof(float)*4);

    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(Utils::CustomWindow::GetResolution().m_width), static_cast<float>(Utils::CustomWindow::GetResolution().m_height), D3D12_MIN_DEPTH, D3D12_MAX_DEPTH };
    RECT scissorRect = { 0L, 0L, static_cast<long>(Utils::CustomWindow::GetResolution().m_width), static_cast<long>(Utils::CustomWindow::GetResolution().m_height) };
    D3D12Render::D3D12SimpleMaterialPtr simpleMaterial = std::make_shared<D3D12Render::D3D12SimpleMaterial>(gpu->GetDevice());
    simpleMaterial->SetConstantBuffer(dynamicConstantBufferID);
    simpleMaterial->SetTexture(texture256ID);

    D3D12Render::D3D12GpuRenderTask viewportQuadRenderTask;
    viewportQuadRenderTask.m_simpleMaterial = simpleMaterial;
    viewportQuadRenderTask.m_viewport = viewport;
    viewportQuadRenderTask.m_scissorRect = scissorRect;
    viewportQuadRenderTask.m_vertexBufferResourceID = vertexBufferResourceID;
    viewportQuadRenderTask.m_indexBufferResourceID = indexBufferResourceID;
    viewportQuadRenderTask.m_vertexCount = g_verticesCount;
    viewportQuadRenderTask.m_vertexSize = g_vertexSize;
    viewportQuadRenderTask.m_indexCount = g_indexCount;
  
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

            {
                DirectX::XMMATRIX transform = DirectX::XMMatrixTranslation(sinf(accumulatedTime), cosf(accumulatedTime), 0.0f);
                gpu->UpdateDynamicConstantBuffer(dynamicConstantBufferID, &transform);
            }

            // Record the command list
            {
                gpu->AddRenderTask(clearRTRenderTask);
                gpu->AddRenderTask(viewportQuadRenderTask);
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