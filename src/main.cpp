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
    constexpr float M_PI     = 3.14159265358979323846f;
    constexpr float M_PI_2   = M_PI * 0.5f;



    // NOTE:  Assuming working directory contains the data folder
    const char* g_texture256FileName = "./data/texture_256.png";
    const char* g_texture1024FileName = "./data/texture_1024.jpg";

    struct Mesh
    {
        const static size_t VertexElemsCount = 5;
        const static size_t VertexSize       = VertexElemsCount * sizeof(float);

        struct Vertex
        {
            Float3 m_position;
            Float2 m_uv;
        };

        std::vector<Vertex>     m_vertices;
        std::vector<uint16_t>   m_indices;

        Mesh()
        {}

        Mesh(size_t verticesCount, size_t indicesCount) :    m_vertices(verticesCount * VertexElemsCount), 
                                                            m_indices(indicesCount)
        {
        }

        size_t VertexBufferSizeInBytes() const { return m_vertices.size() * VertexSize; }
        size_t IndexBufferSizeInBytes() const { return m_indices.size() * sizeof(uint16_t); }
    };

    // TODO move to utils
    Float3 SphericalToCartersian(float longitude, float latitude, float altitude = 1.0f)
    {
        const float sinLat = sinf(latitude);
        const float sinLon = sinf(longitude);

        const float cosLat = cosf(latitude);
        const float cosLon = cosf(longitude);

        Float3 cartesianCoordinates;
        cartesianCoordinates.x = sinLat * cosLon;
        cartesianCoordinates.y = cosLat;
        cartesianCoordinates.z = sinLat * sinLon;
        return cartesianCoordinates * altitude;
    }

    // TODO move these mesh functions to a file
    Mesh CreatePlane()
    {
        Mesh planeMesh;

        planeMesh.m_vertices = 
        { 
            // Positions                UVs
            { { -0.5f, -0.5f, 0.0f },   { 0.0f, 1.0f } },
            { { -0.5f, 0.5f, 0.0f },    { 0.0f, 0.0f } },
            { { 0.5f, 0.5f, 0.0f },     { 1.0f, 0.0f } },
            { { 0.5f, -0.5f, 0.0f },    { 1.0f, 1.0f } }
        };

        planeMesh.m_indices = { 0, 1, 2, 0, 2, 3 };

        return planeMesh;
    }

    // NOTE: Check https://github.com/caosdoar/spheres
    // Review of ways of creating a mesh sphere by @caosdoar
    // TODO fix uv issues
    Mesh CreateSphere(unsigned int parallelsCount = 2, unsigned int meridiansCount = 4)
    {
        assert(parallelsCount > 1 && meridiansCount > 3);

        const unsigned int polesCount = 2; // north and south pole vertices
        const unsigned int verticesCount = parallelsCount * meridiansCount + polesCount;
        const unsigned int indicesPerTri = 3;
        const unsigned int indicesCount = indicesPerTri * (2 * meridiansCount * (parallelsCount - 1) + polesCount * meridiansCount);
        Mesh mesh(parallelsCount * meridiansCount, indicesCount);

        // parallels = latitude = altitude = phi 
        // meridians = longitude = azimuth = theta
        const float latitudeDiff = M_PI / (parallelsCount + 1);
        const float longitudeDiff = 2.0f * M_PI / meridiansCount;

        // Build sphere rings
        size_t currentVertexIndex = 0;
        size_t currentPrimitive = 0;
        const uint16_t parallelVerticesCount = static_cast<uint16_t>(meridiansCount);
        
        for (unsigned int j = 0; j < parallelsCount; ++j)
        {
            // Build rings vertices
            uint16_t verticesIndexed = static_cast<uint16_t>(meridiansCount * j);
            const float latitude = (j+1) * latitudeDiff;
            for (uint16_t i = 0; i < meridiansCount; ++i, ++currentVertexIndex)
            {
                const float longitude = i * longitudeDiff;
                const Mesh::Vertex vertex
                { 
                    SphericalToCartersian(longitude, latitude), 
                    Float2(latitude, longitude) // NOTE: this mapping has horrendous distortions on the poles
                };
                auto& currentVertex = mesh.m_vertices[currentVertexIndex];
                currentVertex = vertex;

                // Build rings indices
                if (j < parallelsCount -1)
                {
                    const bool isLastRingQuad = i == meridiansCount - 1;

                    const uint16_t vertexN1 = !isLastRingQuad ? verticesIndexed + parallelVerticesCount + i + 1 : verticesIndexed + parallelVerticesCount;
                    mesh.m_indices[currentPrimitive++] = verticesIndexed + i;
                    mesh.m_indices[currentPrimitive++] = !isLastRingQuad? (verticesIndexed + i + 1) : verticesIndexed;
                    mesh.m_indices[currentPrimitive++] = vertexN1;

                    mesh.m_indices[currentPrimitive++] = verticesIndexed + i;
                    mesh.m_indices[currentPrimitive++] = vertexN1;
                    mesh.m_indices[currentPrimitive++] = verticesIndexed + parallelVerticesCount + i;
                }
            }
        }

        // Build poles
        mesh.m_vertices[verticesCount - 2] = { Float3(0.0f, 1.0f, 0.0f), Float2(0.0f, 0.0f) };
        mesh.m_vertices[verticesCount - 1] = { Float3(0.0f, -1.0f, 0.0f), Float2(0.0f, 1.0f) };
        for (uint16_t i = 0; i < meridiansCount; ++i)
        {
            mesh.m_indices[currentPrimitive++] = static_cast<uint16_t>(verticesCount - 2);
            mesh.m_indices[currentPrimitive++] = i == meridiansCount - 1? 0 : i + 1;
            mesh.m_indices[currentPrimitive++] = i;
        }

        const uint16_t verticesBuilt = static_cast<uint16_t>((parallelsCount - 1) * meridiansCount);
        for (uint16_t i = 0; i < meridiansCount; ++i)
        {
            mesh.m_indices[currentPrimitive++] = static_cast<uint16_t>(verticesCount - 1);
            mesh.m_indices[currentPrimitive++] = verticesBuilt + i;
            mesh.m_indices[currentPrimitive++] = i == meridiansCount - 1 ? verticesBuilt : verticesBuilt + i + 1;
        }

        return mesh;
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
    
    // TODO Create two spheres, two boxes and a plane
    // Create scene cpu resources 
    Camera camera(Float3(0.0f, 0.0f, -5.0f));
    //Mesh plane = CreatePlane();
    Mesh sphere = CreateSphere(20, 20);

    // Load scene gpu resources
    const D3D12Render::D3D12ResourceID vertexBufferResourceID = D3D12Render::CreateD3D12Buffer(sphere.m_vertices.data(), sphere.VertexBufferSizeInBytes(), L"vb - Viewport Quad", gpu);
    const D3D12Render::D3D12ResourceID indexBufferResourceID = D3D12Render::CreateD3D12Buffer(sphere.m_indices.data(), sphere.IndexBufferSizeInBytes(), L"ib - Viewport Quad", gpu);
    const D3D12Render::D3D12ResourceID texture256ID = D3D12Render::CreateD3D12Texture(g_texture256FileName, L"texture2d - Texture 256", gpu);
    const D3D12Render::D3D12ResourceID dynamicConstantBufferID = gpu->CreateDynamicConstantBuffer(sizeof(DirectX::XMFLOAT4X4));
    gpu->ExecuteCopyCommands();

    // Create tasks for the gpu
    D3D12Render::D3D12GpuRenderTask clearRTRenderTask;
    clearRTRenderTask.m_simpleMaterial = nullptr;
    const float clearColor[4]{ 0.0f, 0.2f, 0.4f, 1.0f };
    memcpy(clearRTRenderTask.m_clearColor, clearColor, sizeof(float)*4);

    D3D12_VIEWPORT viewport = 
    { 
        0.0f, 0.0f, 
        static_cast<float>(Utils::CustomWindow::GetResolution().m_width), 
        static_cast<float>(Utils::CustomWindow::GetResolution().m_height), 
        D3D12_MIN_DEPTH, D3D12_MAX_DEPTH 
    };
    RECT scissorRect = 
    { 
        0L, 0L, 
        static_cast<long>(Utils::CustomWindow::GetResolution().m_width), 
        static_cast<long>(Utils::CustomWindow::GetResolution().m_height) 
    };
    D3D12Render::D3D12SimpleMaterialPtr simpleMaterial = std::make_shared<D3D12Render::D3D12SimpleMaterial>(gpu->GetDevice());
    simpleMaterial->SetConstantBuffer(dynamicConstantBufferID);
    simpleMaterial->SetTexture(texture256ID);

    D3D12Render::D3D12GpuRenderTask viewportQuadRenderTask;
    viewportQuadRenderTask.m_simpleMaterial = simpleMaterial;
    viewportQuadRenderTask.m_viewport = viewport;
    viewportQuadRenderTask.m_scissorRect = scissorRect;
    viewportQuadRenderTask.m_vertexBufferResourceID = vertexBufferResourceID;
    viewportQuadRenderTask.m_indexBufferResourceID = indexBufferResourceID;
    viewportQuadRenderTask.m_vertexCount = sphere.m_vertices.size();
    viewportQuadRenderTask.m_vertexSize = Mesh::VertexSize;
    viewportQuadRenderTask.m_indexCount = sphere.m_indices.size();
  
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
                const Float3 origin(0.0f, 3.0f, -2.0f);
                const Float3 destination(0.0f, -3.0f, -2.0f);
                const float interpolator = sinf(accumulatedTime) * 0.5f + 0.5f;
                const Float3 lerpedPosition = Float3::Lerp(origin, destination, interpolator);

                camera.TranslateLookingAt(lerpedPosition, Float3::Zero);
            }

            // Update gpu resources
            {
                const Matrix44& localToWorld = Matrix44::Identity;
                const Matrix44 localToClip = (localToWorld * camera.WorldToCamera() * camera.CameraToClip()).Transpose();

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