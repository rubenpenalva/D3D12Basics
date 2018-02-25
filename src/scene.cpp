#include "scene.h"

// Third party libraries
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

// c++ libraries
#include <fstream>

using namespace D3D12Basics;

SimpleMaterialData::SimpleMaterialData(const std::wstring& textureFileName)
{
    std::fstream file(textureFileName.c_str(), std::ios::in | std::ios::binary);
    assert(file.is_open());
    const auto fileStart = file.tellg();
    file.ignore(std::numeric_limits<std::streamsize>::max());
    const auto fileSize = file.gcount();
    file.seekg(fileStart);
    std::vector<char> buffer(fileSize);
    file.read(&buffer[0], fileSize);


    const stbi_uc* bufferPtr = reinterpret_cast<const stbi_uc*>(&buffer[0]);
    const int bufferLength = static_cast<int>(fileSize);
    int textureChannelsCount;
    const int requestedChannelsCount = 4;
    m_textureData = stbi_load_from_memory(bufferPtr, bufferLength, &m_textureWidth,
                                          &m_textureHeight, &textureChannelsCount, 
                                          requestedChannelsCount);
    assert(m_textureData);

    m_textureSizeBytes = m_textureWidth * m_textureHeight * requestedChannelsCount;
}

SimpleMaterialData::~SimpleMaterialData()
{
    stbi_image_free(m_textureData);
}

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
// TODO optimization proposed by @caosdoar: cache the angles to avoid unnecessary calculations
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
                Float2(longitude * M_RCP_2PI, latitude * M_RCP_PI) // NOTE: this mapping has horrendous distortions on the poles
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