#include "meshgenerator.h"

using namespace D3D12Basics;

MeshData D3D12Basics::CreatePlane(const VertexDesc& vertexDesc, const Float4& uvScaleOffset)
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
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w
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

    if (vertexDesc.m_tangent_bitangent)
    {
        const size_t tangentsElementsCount = 3;
        streams.AddStream(tangentsElementsCount,
        {
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f,
            1.0f, 0.0f, 0.0f
        });
        const size_t bitangentsElementsCount = 3;
        streams.AddStream(bitangentsElementsCount,
        {
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f
        });
    }
    const auto vertexElementsCount = streams.VertexElementsCount();

    return MeshData{streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount};
}

// NOTE: Check https://github.com/caosdoar/spheres
// Review of ways of creating a mesh sphere by @caosdoar
// TODO fix uv issues
// TODO optimization proposed by @caosdoar: cache the angles to avoid unnecessary calculations
// TODO templatize this function depending on its configuration
MeshData D3D12Basics::CreateSphere(const VertexDesc& vertexDesc, const Float4& uvScaleOffset, 
                                   unsigned int parallelsCount, unsigned int meridiansCount)
{
    // TODO tangents generation not supported yet
    assert(parallelsCount > 1 && meridiansCount > 3);

    // Note this adds another meridian that will be used to fix the uv mapping of the
    // meridians last vertices.
    meridiansCount++;

    const unsigned int polesCount = 2; // north and south pole vertices
    const unsigned int verticesCount = parallelsCount * meridiansCount + polesCount;
    const unsigned int indicesPerTri = 3;
    const unsigned int indicesCount = indicesPerTri * meridiansCount * (2 * (parallelsCount - 1) + polesCount);

    const size_t positionElementsCount = 3;
    std::vector<float> positions((parallelsCount * meridiansCount + polesCount) * positionElementsCount);
    std::vector<float> uvs;
    const size_t uvElementsCount = 2;
    if (vertexDesc.m_uv0)
    {
        uvs.resize((parallelsCount * meridiansCount + polesCount) * uvElementsCount);
    }
    const size_t normalsElementsCount = 3;
    std::vector<float> normals;
    if (vertexDesc.m_normal)
    {
        normals.resize(positions.size());
    }
    const size_t tangentElementsCount = 3;
    const size_t bitangentElementsCount = 3;
    std::vector<float> tangents;
    std::vector<float> bitangents;
    if (vertexDesc.m_tangent_bitangent)
    {
        tangents.resize((parallelsCount * meridiansCount + polesCount) * tangentElementsCount);
        bitangents.resize((parallelsCount * meridiansCount + polesCount) * bitangentElementsCount);
    }
    std::vector<uint16_t> indices(indicesCount);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    const float latitudeDiff = M_PI / (parallelsCount + 1);
    const float longitudeDiff = 2.0f * M_PI / (meridiansCount - 1);

    // Build sphere rings
    size_t currentVertexIndex = 0;
    size_t currentPrimitive = 0;
    const uint16_t parallelVerticesCount = static_cast<uint16_t>(meridiansCount);

    const Float2 uvScale = Float2{ uvScaleOffset.x, uvScaleOffset.y };
    const Float2 uvOffset = Float2{ uvScaleOffset.z, uvScaleOffset.w };

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
            if (vertexDesc.m_uv0)
            {
                auto uv = i == meridiansCount - 1 ? Float2(1.0f, latitude * M_RCP_PI) :
                                                    Float2(longitude * M_RCP_2PI, latitude * M_RCP_PI);
                uv *= uvScale;
                uv += uvOffset;
                memcpy(&uvs[currentVertexIndex * uvElementsCount],
                        &uv, sizeof(Float2));
            }

            if (vertexDesc.m_normal)
            {
                auto normal = position;
                normal.Normalize();
                memcpy(&normals[currentVertexIndex * positionElementsCount], &normal, sizeof(Float3));
            }

            if (vertexDesc.m_tangent_bitangent)
            {
                auto tangent = DDLonSphericalToCartesian(longitude, latitude) * Float3(0.5f, 0.5f, 0.5f);
                tangent.Normalize();
                memcpy(&tangents[currentVertexIndex * tangentElementsCount],
                        &tangent, sizeof(Float3));
                auto bitangent = DDLatSphericalToCartesian(longitude, latitude) * Float3(0.5f, 0.5f, 0.5f);
                bitangent.Normalize();
                memcpy(&bitangents[currentVertexIndex * bitangentElementsCount],
                        &bitangent, sizeof(Float3));
            }

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
    position = Float3(0.0f, -0.5f, 0.0f);
    memcpy(&positions[(verticesCount - 1) * positionElementsCount], &position, sizeof(Float3));
    if (vertexDesc.m_uv0)
    {
        auto uv = Float2(0.0f, 0.0f);
        memcpy(&uvs[(verticesCount - 2) * uvElementsCount], &uv, sizeof(Float2));
        uv = Float2(0.0f, 1.0f);
        uv *= uvScale;
        uv += uvOffset;
        memcpy(&uvs[(verticesCount - 1) * uvElementsCount], &uv, sizeof(Float2));
    }
    if (vertexDesc.m_normal)
    {
        auto normal = Float3(0.0f, 1.0f, 0.0f);
        memcpy(&normals[(verticesCount - 2) * normalsElementsCount], &normal, sizeof(Float3));
        normal = Float3(0.0f, -1.0f, 0.0f);
        memcpy(&normals[(verticesCount - 1) * normalsElementsCount], &normal, sizeof(Float3));
    }
    if (vertexDesc.m_tangent_bitangent)
    {
        auto tangent = Float3(1.0f, 0.0f, 0.0f);
        memcpy(&tangents[(verticesCount - 2) * tangentElementsCount], &tangent, sizeof(Float3));
        tangent = Float3(-1.0f, 0.0f, 0.0f);
        memcpy(&tangents[(verticesCount - 1) * tangentElementsCount], &tangent, sizeof(Float3));
        auto bitangent = Float3(0.0f, 0.0f, 1.0f);
        memcpy(&bitangents[(verticesCount - 2) * bitangentElementsCount], &bitangent, sizeof(Float3));
        bitangent = Float3(0.0f, 0.0f, -1.0f);
        memcpy(&bitangents[(verticesCount - 1) * bitangentElementsCount], &bitangent, sizeof(Float3));
    }
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
    if (vertexDesc.m_uv0)
        streams.AddStream(uvElementsCount, std::move(uvs));
    if (vertexDesc.m_normal)
        streams.AddStream(normalsElementsCount, std::move(normals));
    if (vertexDesc.m_tangent_bitangent)
    {
        streams.AddStream(tangentElementsCount, std::move(tangents));
        streams.AddStream(bitangentElementsCount, std::move(bitangents));
    }
    const auto vertexElementsCount = streams.VertexElementsCount();

    return MeshData{ streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount };
}

MeshData D3D12Basics::CreateCube(const VertexDesc& vertexDesc, const Float4& uvScaleOffset, 
                                    Cube_TexCoord_MappingType /*texcoordType*/)
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
            // Back
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,

            //Front
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,

            //Left
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,

            //Right
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,

            //Bottom
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,

            //Top 
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
            0.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 0.0f * uvScaleOffset.y + uvScaleOffset.w,
            1.0f * uvScaleOffset.x + uvScaleOffset.z, 1.0f * uvScaleOffset.y + uvScaleOffset.w,
        });
    }

    if (vertexDesc.m_normal)
    {
        const size_t normalElementCount = 3;
        streams.AddStream(normalElementCount,
        {
                // Back
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,

                // Front
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,

                // Left
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,

                // Right
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,

                //Bottom
                0.0f, -1.0f, 0.0f,
                0.0f, -1.0f, 0.0f,
                0.0f, -1.0f, 0.0f,
                0.0f, -1.0f, 0.0f,

                //Top
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
        });
    }

    if (vertexDesc.m_tangent_bitangent)
    {
        const size_t tangentElementCount = 3;
        streams.AddStream(tangentElementCount,
        {
                // Back
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,
                -1.0f, 0.0f, 0.0f,

                // Front
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,

                // Left
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,

                // Right
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,

                //Bottom
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,

                //Top
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
                1.0f, 0.0f, 0.0f,
        });

        const size_t bitangentElementCount = 3;
        streams.AddStream(bitangentElementCount,
            {
                // Back
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,

                // Front
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,

                // Left
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,

                // Right
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,
                0.0f, 1.0f, 0.0f,

                //Bottom
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,
                0.0f, 0.0f, -1.0f,

                //Top
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
                0.0f, 0.0f, 1.0f,
            });
    }
    const auto vertexElementsCount = streams.VertexElementsCount();

    return MeshData{ streams.GetStreams(), std::move(indices), verticesCount, vertexElementsCount * sizeof(float), vertexElementsCount };
}