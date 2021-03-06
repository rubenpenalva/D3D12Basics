// project includes
#include "utils.h"

// c++ includes
#include <vector>

namespace D3D12Basics
{
    // TODO implement the rest of the mappings
    enum class Cube_TexCoord_MappingType
    {
        Cube_TexCoord_UV_SingleFace,
        Cube_TexCoord_UV_OrigamiFaces,
        Cube_TexCoord_UVW_CubeFaces
    };

    // TODO move vertex composition to compile time?
    MeshData CreatePlane(const VertexDesc& vertexDesc, const Float4& uvScaleOffset);

    // NOTE: Check https://github.com/caosdoar/spheres
    // Review of ways of creating a mesh sphere by @caosdoar
    // TODO fix uv issues
    MeshData CreateSphere(const VertexDesc& vertexDesc, const Float4& uvScaleOffset, unsigned int parallelsCount = 2, unsigned int meridiansCount = 4);

    MeshData CreateCube(const VertexDesc& vertexDesc, const Float4& uvScaleOffset,
                        Cube_TexCoord_MappingType texcoordType = Cube_TexCoord_MappingType::Cube_TexCoord_UV_SingleFace);
}