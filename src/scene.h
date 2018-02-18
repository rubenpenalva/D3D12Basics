// project includes
#include "utils.h"

// c++ includes
#include <vector>
#include <memory>

namespace D3D12Basics
{
    enum class Cube_TexCoord_MappingType
    {
        Cube_TexCoord_UV_SingleFace,
        Cube_TexCoord_UV_OrigamiFaces,
        Cube_TexCoord_UVW_CubeFaces
    };

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

    struct SimpleMaterialData
    {
    public:
        SimpleMaterialData(const std::wstring& textureFileName);
        
        ~SimpleMaterialData();

        unsigned char*  m_textureData;
        unsigned int    m_textureSizeBytes;
        int             m_textureWidth;
        int             m_textureHeight;
    };
    using SimpleMaterialDataPtr = std::shared_ptr<SimpleMaterialData>;

    struct Model
    {
        std::wstring    m_name;
        Matrix44        m_transform;
        Mesh            m_mesh;
        std::wstring    m_simpleMaterialTextureFileName;
    };

    class Camera
    {
    public:
        // NOTE Operating on a LH coordinate system
        // NOTE fov is in radians
        Camera(const Float3& position, const Float3& target = Float3::Zero, float fov = M_PI_2 - M_PI_8,
                float aspectRatio = 1.6f, float nearPlane = 0.1f, float farPlane = 1000.0f, 
                const Float3& up = Float3::UnitY);

        void TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up = Float3::UnitY);

        const Matrix44& CameraToClip() const { return m_cameraToClip; }

        const Matrix44& WorldToCamera() const { return m_worldToCamera; }

    private:
        Matrix44 m_worldToCamera;
        Matrix44 m_cameraToClip;
    };

    struct Scene
    {
        Camera              m_camera;
        std::vector<Model>  m_models;
    };

    Mesh CreatePlane();

    // NOTE: Check https://github.com/caosdoar/spheres
    // Review of ways of creating a mesh sphere by @caosdoar
    // TODO fix uv issues
    Mesh CreateSphere(unsigned int parallelsCount = 2, unsigned int meridiansCount = 4);

    Mesh CreateCube(Cube_TexCoord_MappingType texcoordType = Cube_TexCoord_MappingType::Cube_TexCoord_UV_SingleFace);
}