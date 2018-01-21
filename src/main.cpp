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
#include "DirectXTK12/Inc/SimpleMath.h"

namespace
{
    using Float2    = DirectX::SimpleMath::Vector2;
    using Float3    = DirectX::SimpleMath::Vector3;
    using Matrix44  = DirectX::SimpleMath::Matrix;

    // NOTE: https://www.gnu.org/software/libc/manual/html_node/Mathematical-Constants.html
    constexpr auto M_PI     = 3.14159265358979323846;
    constexpr auto M_PI_2   = M_PI * 0.5;

    const size_t g_vertexElemsCount = 5;
    const size_t g_verticesCount = 4;
    float g_vertices[g_verticesCount * g_vertexElemsCount]
    {
        //Position              UV
        0.5f, 0.5f, 0.0f,       1.0f, 0.0f,
        0.5f, -0.5f, 0.0f,      1.0f, 1.0f,
        -0.5f, 0.5f, 0.0f,      0.0f, 0.0f,
        -0.5f, -0.5f, 0.0f,     0.0f, 1.0f,
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

        Mesh()
        {}

        Mesh(unsigned int verticesCount, unsigned int indicesCount) :   m_vertexPositions(verticesCount), m_vertexUVs(verticesCount),
                                                                        m_indices(indicesCount)
        {
            m_vertexPositions.resize(verticesCount);
            m_vertexUVs.resize(verticesCount);

            m_indices.resize(indicesCount);
        }
    };

    Float3 SphericalToCartersian(float longitude, float latitude, float altitude = 1.0f)
    {
        const float sinLat = sinf(latitude);
        const float sinLon = sinf(longitude);
        
        const float cosLat = cosf(latitude);
        const float cosLon = cosf(longitude);

        Float3 cartesianCoordinates;
        cartesianCoordinates.x = sinLat * cosLon;
        cartesianCoordinates.y = cosLat;
        cartesianCoordinates.x = sinLat * sinLon;
        return cartesianCoordinates * altitude;
    }

    Mesh CreatePlane()
    {
        Mesh planeMesh;

        planeMesh.m_vertexPositions = { { -0.5f, -0.5f, 0.0f }, { -0.5f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.0f }, { 0.5f, -0.5f, 0.0f } };
        planeMesh.m_vertexUVs = { {0.0f, 1.0f}, { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f } };

        planeMesh.m_indices = { 0, 1, 2, 0, 2, 3 };

        return planeMesh;
    }

    class Camera
    {
    public:
        // NOTE Operating on a LH coordinate system
        // NOTE fov is in radians
        Camera( const Float3& position, const Float3& target = Float3::Zero, float fov = M_PI_2, 
                float aspectRatio = Utils::CustomWindow::GetResolution().m_aspectRatio,
                float nearPlane = 0.1f, float farPlane = 1000.0f, const Float3& up = Float3::UnitY)
        {
            m_cameraToClip = Matrix44::CreatePerspectiveFieldOfViewLH(fov, aspectRatio, nearPlane, farPlane);

            m_worldToCamera = Matrix44::CreateLookAtLH(position, target, up);
        }

        void TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up = Float3::UnitY)
        {
            m_worldToCamera = Matrix44::CreateLookAtLH(position, target, up);
        }

        const Matrix44& CameraToClip() const { return m_cameraToClip; }
        
        const Matrix44& WorldToCamera() const { return m_worldToCamera; }

    private:
        Matrix44 m_worldToCamera;
        Matrix44 m_cameraToClip;
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
    
    // Create scene cpu resources 
    Camera camera(Float3(0.0f, 0.0f, -1.0f));

    // Load scene gpu resources
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

            // Update scene
            {
                const Float3 origin(0.0f, 0.0f, -1.0f);
                const Float3 destination(0.0f, 0.0f, -2.0f);
                const float interpolator = sinf(accumulatedTime) * 0.5f + 0.5f;
                const Float3 lerpedPosition = Float3::Lerp(origin, destination, interpolator);

                camera.TranslateLookingAt(lerpedPosition, Float3::Zero);
            }

            // Update gpu resources
            {
                const Matrix44& localToWorld = Matrix44::Identity;
                const Matrix44 localToClip = localToWorld * camera.WorldToCamera() * camera.CameraToClip();

                gpu->UpdateDynamicConstantBuffer(dynamicConstantBufferID, &localToClip);
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