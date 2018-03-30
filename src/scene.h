// project includes
#include "utils.h"

// c++ includes
#include <vector>
#include <memory>
#include <optional>

// third party includes
#include "assimp/Importer.hpp"

namespace D3D12Basics
{
    class Camera
    {
    public:
        // NOTE Operating on a LH coordinate system
        // NOTE fov is in radians
        Camera();

        void TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up = Float3::UnitY);

        const Matrix44& CameraToClip() const { return m_cameraToClip; }

        const Matrix44& WorldToCamera() const { return m_worldToCamera; }

        const Matrix44& CameraToWorld() const { return m_cameraToWorld; }

        const Float3& Position() const { return m_position; }
        const Float3& Forward() const { return m_forward; }

    private:
        Matrix44 m_worldToCamera;
        Matrix44 m_cameraToWorld;

        Matrix44 m_cameraToClip;

        Float3 m_position;
        Float3 m_forward;

        void UpdateCameraToWorld(const Float3& position);
    };

    struct Material
    {
        static const Float3 m_diffuseColor;

        std::optional<std::wstring> m_diffuseTexture;
        std::optional<std::wstring> m_specularTexture;
        std::optional<std::wstring> m_normalsTexture;
    };

    class TextureData
    {
    public:
        TextureData(const D3D12_RESOURCE_DESC& resourceDesc,
                    std::unique_ptr<uint8_t[]> rawData,
                    std::vector<D3D12_SUBRESOURCE_DATA>&& subresources) :   m_resourceDesc(resourceDesc), m_rawData(std::move(rawData)),
                                                                            m_subresources(subresources)
        {}

        const D3D12_RESOURCE_DESC& GetDesc() const { return m_resourceDesc; };

        const std::vector<D3D12_SUBRESOURCE_DATA>& GetSubResources() const { return m_subresources; }

    private:
        D3D12_RESOURCE_DESC m_resourceDesc;

        std::unique_ptr<uint8_t[]>          m_rawData;
        std::vector<D3D12_SUBRESOURCE_DATA> m_subresources;
    };

    struct Model
    {
        enum class Type
        {
            MeshFile,
            Plane,
            Sphere,
            Cube
        };

        std::wstring    m_name;
        Type            m_type;
        size_t          m_id;

        Matrix44 m_transform;
        Material m_material;
    };

    struct Scene
    {
        std::wstring m_sceneFile;

        Camera                m_camera;
        std::vector<Model>    m_models;
    };

    class SceneLoader
    {
    public:
        SceneLoader(const std::wstring& sceneFile, Scene& scene, const std::wstring& dataWorkingPath);

        TextureData LoadTextureData(const std::wstring& textureFile);

        Mesh LoadMesh(size_t modelId);

    private:
        Assimp::Importer m_assImporter;

        Scene& m_outScene;
    };
}