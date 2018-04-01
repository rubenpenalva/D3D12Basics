#include "meshgenerator.h"

using namespace D3D12Basics;

Mesh D3D12Basics::CreatePlane(const VertexDesc& vertexDesc)
{
    std::vector<uint16_t> indices = { 0, 1, 2, 0, 2, 3 };
    const size_t verticesCount = 4;

    VertexStreams streams;
    const size_t positionElementsCount = 3;
    streams.AddStream(positionElementsCount,
    {
        // Positions
        -0.5f, -0.5f, 0.0f,
        -0.5f, 0.5f, 0.0f,
        0.5f, 0.5f, 0.0f,
        0.5f, -0.5f, 0.0f
    });

    if (vertexDesc.m_uv0)
    {
        const size_t uvElementsCount = 2;
        streams.AddStream(uvElementsCount, 
        {
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f
        });
    }

    if (vertexDesc.m_normal)
    {
        const size_t normalsElementsCount = 3;
        streams.AddStream(normalsElementsCount,
        {
            0.0f, 0.0f, -1.0f,
            0.0f, 0.0f, -1.0f,
            0.0f, 0.0f, -1.0f,
            0.0f, 0.0f, -1.0f
        });
    }

    if (vertexDesc.m_tangent)
    {
        const size_t tangentsElementsCount = 3;
        streams.AddStream(tangentsElementsCount,
        {
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f
        });
    }
    const auto vertexElementsCount = streams.VertexElementsCount();

    return Mesh {streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount};
}

// NOTE: Check https://github.com/caosdoar/spheres
// Review of ways of creating a mesh sphere by @caosdoar
// TODO fix uv issues
// TODO optimization proposed by @caosdoar: cache the angles to avoid unnecessary calculations
// TODO use vertexDesc
Mesh D3D12Basics::CreateSphere(const VertexDesc& /*vertexDesc*/, unsigned int parallelsCount, unsigned int meridiansCount)
{
    assert(parallelsCount > 1 && meridiansCount > 3);

    // Note this adds another meridian that will be used to fix the uv mapping of the
    // meridians last vertices.
    meridiansCount++;

    const unsigned int polesCount = 2; // north and south pole vertices
    const unsigned int verticesCount = parallelsCount * meridiansCount + polesCount;
    const unsigned int indicesPerTri = 3;
    const unsigned int indicesCount = indicesPerTri * meridiansCount * (2 * (parallelsCount - 1) + polesCount);

    size_t positionElementsCount = 3;
    std::vector<float> positions((parallelsCount * meridiansCount + polesCount) * positionElementsCount);
    const size_t uvElementsCount = 2;
    std::vector<float> uvs((parallelsCount * meridiansCount + polesCount) * uvElementsCount);
    std::vector<uint16_t> indices(indicesCount);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    const float latitudeDiff = M_PI / (parallelsCount + 1);
    const float longitudeDiff = 2.0f * M_PI / (meridiansCount - 1);

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
            auto position = SphericalToCartersian(longitude, latitude) * Float3(0.5f, 0.5f, 0.5f);
            memcpy(&positions[currentVertexIndex * positionElementsCount], 
                   &position, sizeof(Float3));
            
            // NOTE: this mapping has horrendous distortions on the poles
            auto uv = i == meridiansCount - 1?  Float2(1.0f, latitude * M_RCP_PI) : 
                                                Float2(longitude * M_RCP_2PI, latitude * M_RCP_PI);
            memcpy(&uvs[currentVertexIndex * uvElementsCount],
                   &uv, sizeof(Float2));

            // Build rings indices
            if (j < parallelsCount - 1)
            {
                const bool isLastRingQuad = i == meridiansCount - 1;

                const uint16_t vertexN1 = !isLastRingQuad ? verticesIndexed + parallelVerticesCount + i + 1 : verticesIndexed + parallelVerticesCount;
                indices[currentPrimitive++] = verticesIndexed + i;
                indices[currentPrimitive++] = !isLastRingQuad ? (verticesIndexed + i + 1) : verticesIndexed;
                indices[currentPrimitive++] = vertexN1;

                indices[currentPrimitive++] = verticesIndexed + i;
                indices[currentPrimitive++] = vertexN1;
                indices[currentPrimitive++] = verticesIndexed + parallelVerticesCount + i;
            }
        }
    }

    // Build poles
    auto position = Float3(0.0f, 0.5f, 0.0f);
    memcpy(&positions[(verticesCount - 2) * positionElementsCount], &position, sizeof(Float3));
    auto uv = Float2(0.0f, 0.0f);
    memcpy(&uvs[(verticesCount - 2) * uvElementsCount], &uv, sizeof(Float2));
    position = Float3(0.0f, -0.5f, 0.0f);
    memcpy(&positions[(verticesCount - 1) * positionElementsCount], &position, sizeof(Float3));
    uv = Float2(0.0f, 1.0f);
    memcpy(&uvs[(verticesCount - 1) * uvElementsCount], &uv, sizeof(Float2));

    for (uint16_t i = 0; i < meridiansCount; ++i)
    {
       indices[currentPrimitive++] = static_cast<uint16_t>(verticesCount - 2);
       indices[currentPrimitive++] = i == meridiansCount - 1 ? 0 : i + 1;
       indices[currentPrimitive++] = i;
    }

    const uint16_t verticesBuilt = static_cast<uint16_t>((parallelsCount - 1) * meridiansCount);
    for (uint16_t i = 0; i < meridiansCount; ++i)
    {
        indices[currentPrimitive++] = static_cast<uint16_t>(verticesCount - 1);
        indices[currentPrimitive++] = verticesBuilt + i;
        indices[currentPrimitive++] = i == meridiansCount - 1 ? verticesBuilt : verticesBuilt + i + 1;
    }
    
    VertexStreams streams;
    streams.AddStream(positionElementsCount, std::move(positions));
    streams.AddStream(uvElementsCount, std::move(uvs));
    const auto vertexElementsCount = streams.VertexElementsCount();

    return Mesh{ streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount };
}

Mesh D3D12Basics::CreateCube(const VertexDesc& vertexDesc, Cube_TexCoord_MappingType /*texcoordType*/)
{
    std::vector<uint16_t> indices =
    {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };
    const size_t verticesCount = 24;

    VertexStreams streams;
    const size_t positionElementsCount = 3;
    streams.AddStream(positionElementsCount,
    {
        // Positions
        // Back
        -0.5f, -0.5f, -0.5f,
        -0.5f, 0.5f, -0.5f,
        0.5f, 0.5f, -0.5f,
        0.5f, -0.5f, -0.5f,

        // Front
        0.5f, -0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,

        // Left
        -0.5f, -0.5f, 0.5f,
        -0.5f, 0.5f, 0.5f,
        -0.5f, 0.5f, -0.5f,
        -0.5f, -0.5f, -0.5f,

        // Right
        0.5f, -0.5f, -0.5f,
        0.5f, 0.5f, -0.5f,
        0.5f, 0.5f, 0.5f,
        0.5f, -0.5f, 0.5f,

        //Bottom
        0.5f, -0.5f, -0.5f,
        0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,
        -0.5f, -0.5f, -0.5f,

        //Top
        -0.5f, 0.5f, -0.5f,
        -0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, 0.5f,
        0.5f, 0.5f, -0.5f,
    });

    if (vertexDesc.m_uv0)
    {
        const size_t uvElementsCount = 2;
        streams.AddStream(uvElementsCount,
        {
            // UVs
            // Back
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            //Front
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            //Left
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            //Right
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            //Bottom
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,

            //Top
            0.0f, 1.0f,
            0.0f, 0.0f,
            1.0f, 0.0f,
            1.0f, 1.0f,
        });
    }

    // TODO normals

    // TODO tangents

    const auto vertexElementsCount = streams.VertexElementsCount();

    return Mesh{ streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount };
}