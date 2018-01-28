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

#include "scene.h"

using namespace D3D12Basics;

Camera::Camera(const Float3& position, const Float3& target, float fov, float aspectRatio,
    float nearPlane, float farPlane, const Float3& up)
{
    m_cameraToClip = Matrix44::CreatePerspectiveFieldOfViewLH(fov, aspectRatio, nearPlane, farPlane);

    m_worldToCamera = Matrix44::CreateLookAtLH(position, target, up);
}

void Camera::TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up)
{
    m_worldToCamera = Matrix44::CreateLookAtLH(position, target, up);
}

// TODO move these mesh functions to a file
Mesh D3D12Basics::CreatePlane()
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
Mesh D3D12Basics::CreateSphere(unsigned int parallelsCount, unsigned int meridiansCount)
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
        const float latitude = (j + 1) * latitudeDiff;
        for (uint16_t i = 0; i < meridiansCount; ++i, ++currentVertexIndex)
        {
            const float longitude = i * longitudeDiff;
            const Mesh::Vertex vertex
            {
                SphericalToCartersian(longitude, latitude) * Float3(0.5f, 0.5f, 0.5f),
                Float2(latitude, longitude) // NOTE: this mapping has horrendous distortions on the poles
            };
            auto& currentVertex = mesh.m_vertices[currentVertexIndex];
            currentVertex = vertex;

            // Build rings indices
            if (j < parallelsCount - 1)
            {
                const bool isLastRingQuad = i == meridiansCount - 1;

                const uint16_t vertexN1 = !isLastRingQuad ? verticesIndexed + parallelVerticesCount + i + 1 : verticesIndexed + parallelVerticesCount;
                mesh.m_indices[currentPrimitive++] = verticesIndexed + i;
                mesh.m_indices[currentPrimitive++] = !isLastRingQuad ? (verticesIndexed + i + 1) : verticesIndexed;
                mesh.m_indices[currentPrimitive++] = vertexN1;

                mesh.m_indices[currentPrimitive++] = verticesIndexed + i;
                mesh.m_indices[currentPrimitive++] = vertexN1;
                mesh.m_indices[currentPrimitive++] = verticesIndexed + parallelVerticesCount + i;
            }
        }
    }

    // Build poles
    mesh.m_vertices[verticesCount - 2] = { Float3(0.0f, 0.5f, 0.0f), Float2(0.0f, 0.0f) };
    mesh.m_vertices[verticesCount - 1] = { Float3(0.0f, -0.5f, 0.0f), Float2(0.0f, 1.0f) };
    for (uint16_t i = 0; i < meridiansCount; ++i)
    {
        mesh.m_indices[currentPrimitive++] = static_cast<uint16_t>(verticesCount - 2);
        mesh.m_indices[currentPrimitive++] = i == meridiansCount - 1 ? 0 : i + 1;
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

// TODO move these mesh functions to a file
Mesh D3D12Basics::CreateCube(Cube_TexCoord_MappingType /*texcoordType*/)
{
    //Cube_TexCoord_UV_SingleFace,
    //    Cube_TexCoord_UV_OrigamiFaces,
    //    Cube_TexCoord_UVW_CubeFaces

    Mesh cubeMesh;

    cubeMesh.m_vertices =
    {
        // Positions                UVs
        // Back
        { { -0.5f, -0.5f, -0.5f },  { 0.0f, 1.0f } },
        { { -0.5f, 0.5f, -0.5f },   { 0.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f },    { 1.0f, 0.0f } },
        { { 0.5f, -0.5f, -0.5f },   { 1.0f, 1.0f } },

        // Front
        { { 0.5f, -0.5f, 0.5f },    { 0.0f, 1.0f } },
        { { 0.5f, 0.5f, 0.5f },     { 0.0f, 0.0f } },
        { { -0.5f, 0.5f, 0.5f },    { 1.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.5f },   { 1.0f, 1.0f } },

        // Left
        { { -0.5f, -0.5f, 0.5f },   { 0.0f, 1.0f } },
        { { -0.5f, 0.5f, 0.5f },    { 0.0f, 0.0f } },
        { { -0.5f, 0.5f, -0.5f },   { 1.0f, 0.0f } },
        { { -0.5f, -0.5f, -0.5f },  { 1.0f, 1.0f } },

        // Right
        { { 0.5f, -0.5f, -0.5f },   { 0.0f, 1.0f } },
        { { 0.5f, 0.5f, -0.5f },    { 0.0f, 0.0f } },
        { { 0.5f, 0.5f, 0.5f },     { 1.0f, 0.0f } },
        { { 0.5f, -0.5f, 0.5f },    { 1.0f, 1.0f } },

        // Bottom
        { { 0.5f, -0.5f, -0.5f },   { 0.0f, 1.0f } },
        { { 0.5f, -0.5f, 0.5f },    { 0.0f, 0.0f } },
        { { -0.5f, -0.5f, 0.5f },   { 1.0f, 0.0f } },
        { { -0.5f, -0.5f, -0.5f },  { 1.0f, 1.0f } },

        // Top
        { { -0.5f, 0.5f, -0.5f },   { 0.0f, 1.0f } },
        { { -0.5f, 0.5f, 0.5f },    { 0.0f, 0.0f } },
        { { 0.5f, 0.5f, 0.5f },     { 1.0f, 0.0f } },
        { { 0.5f, 0.5f, -0.5f },    { 1.0f, 1.0f } },
    };

    cubeMesh.m_indices = 
    { 
        0, 1, 2, 0, 2, 3, 
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };

    return cubeMesh;
}