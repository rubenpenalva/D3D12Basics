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

// project includes
#include "utils.h"

// c++ includes
#include <vector>

namespace D3D12Basics
{
    struct Mesh
    {
        const static size_t VertexElemsCount = 5;
        const static size_t VertexSize = VertexElemsCount * sizeof(float);

        struct Vertex
        {
            Float3 m_position;
            Float2 m_uv;
        };

        std::vector<Vertex>     m_vertices;
        std::vector<uint16_t>   m_indices;
        std::wstring            m_name;

        Mesh()
        {}

        Mesh(size_t verticesCount, size_t indicesCount) :   m_vertices(verticesCount * VertexElemsCount),
                                                            m_indices(indicesCount)
        {
        }

        size_t VertexBufferSizeInBytes() const { return m_vertices.size() * VertexSize; }
        size_t IndexBufferSizeInBytes() const { return m_indices.size() * sizeof(uint16_t); }
    };

    class Camera
    {
    public:
        // NOTE Operating on a LH coordinate system
        // NOTE fov is in radians
        Camera(const Float3& position, const Float3& target = Float3::Zero, float fov = M_PI_2,
                float aspectRatio = CustomWindow::GetResolution().m_aspectRatio,
                float nearPlane = 0.1f, float farPlane = 1000.0f, const Float3& up = Float3::UnitY);

        void TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up = Float3::UnitY);

        const Matrix44& CameraToClip() const { return m_cameraToClip; }

        const Matrix44& WorldToCamera() const { return m_worldToCamera; }

    private:
        Matrix44 m_worldToCamera;
        Matrix44 m_cameraToClip;
    };

    Mesh CreatePlane();

    // NOTE: Check https://github.com/caosdoar/spheres
    // Review of ways of creating a mesh sphere by @caosdoar
    // TODO fix uv issues
    Mesh CreateSphere(unsigned int parallelsCount = 2, unsigned int meridiansCount = 4);
}