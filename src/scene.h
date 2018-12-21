#pragma once

// project includes
#include "utils.h"

// c++ includes
#include <vector>
#include <memory>

// third party includes
#include "assimp/Importer.hpp"

namespace D3D12Basics
{
    class EntityTransform
    {
    public:
        enum class ProjectionType
        {
            Orthographic,
            Perspective
        };
        // NOTE Operating on a LH coordinate system
        // NOTE fov is in radians
        EntityTransform(ProjectionType projectionType = ProjectionType::Perspective);

        void TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up = Float3::UnitY);

        const Matrix44& LocalToClip() const { return m_localToClip; }

        const Matrix44& WorldToLocal() const { return m_worldToLocal; }

        const Matrix44& LocalToWorld() const { return m_localToWorld; }

        const Float3& Position() const { return m_position; }
        const Float3& Forward() const { return m_forward; }

    private:
        Matrix44 m_worldToLocal;
        Matrix44 m_localToWorld;

        Matrix44 m_localToClip;

        Float3 m_position;
        Float3 m_forward;

        void UpdateLocalToWorld(const Float3& position);
    };

    struct Light
    {
        Light(EntityTransform transform, float intensity) : m_transform(transform), 
                                                            m_intensity(intensity)
        {}

        EntityTransform m_transform;
        float           m_intensity;
    };

    struct Material
    {
       Float3 m_diffuseColor;

       std::wstring m_diffuseTexture;
       std::wstring m_specularTexture;
       std::wstring m_normalsTexture;

       bool m_shadowReceiver;
       bool m_shadowCaster;
    };

    class TextureData
    {
    public:
        TextureData() {}

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
        Float4          m_uvScaleOffset;

        Matrix44 m_transform;
        Matrix44 m_normalTransform;
        Material m_material;
    };

    struct Scene
    {
        std::wstring m_sceneFile;

        EntityTransform     m_camera;
        std::vector<Light>  m_lights;
        std::vector<Model>  m_models;
    };

    class SceneLoader
    {
    public:
        SceneLoader(const std::wstring& sceneFile, Scene& scene, const std::wstring& dataWorkingPath);

        TextureData LoadTextureData(const std::wstring& textureFile);

        MeshData LoadMesh(size_t modelId);

    private:
        Assimp::Importer m_assImporter;

        Scene& m_outScene;

        unsigned int m_assimpModelIdStart;
    };

    class CameraController
    {
    public:
        CameraController(InputController& inputController);

        void Update(EntityTransform& camera, float deltaTime, float totalTime);

    private:
        struct UserCameraState
        {
            bool m_manualMovement = false;

            Float3  m_direction = Float3(0.0f, 0.0f, 1.0f);
            Float3  m_target = Float3{};
            float   m_maxSpeed = 10.0f;
            float   m_maxLookSpeed = 5.0f;
            float   m_speedModifier = 0.0f;
            float   m_speedLookModifier = 0.0f;
        };

        InputController& m_inputController;

        UserCameraState m_cameraState;

        void ProcessMouseInput(const DirectX::Mouse::State& mouseState);

        void ProcessKeyboardInput(const DirectX::Keyboard::State& keyboardState,
            const DirectX::Keyboard::KeyboardStateTracker& keyboardTracker);

        void ProcessGamePadInput(const DirectX::GamePad::State& gamepadState,
            const DirectX::GamePad::ButtonStateTracker& gamepadTracker);

        void ProcessInput();

        void UpdateCamera(EntityTransform& camera, float deltaTime, float totalTime);
    };

    class AppController
    {
    public:
        AppController(const InputController& inputController);

        void Update(CustomWindow& customWindow, bool& quit);

    private:
        const InputController& m_inputController;

        bool ProcessKeyboardInput(const DirectX::Keyboard::State& keyboardState,
                                  const DirectX::Keyboard::KeyboardStateTracker& keyboardTracker,
                                  D3D12Basics::CustomWindow& customWindow);

        bool ProcessGamePadInput(const DirectX::GamePad::ButtonStateTracker& gamepadTracker);
    };

    using TextureDataCache = std::unordered_map<std::wstring, TextureData>;
    using MeshDataCache = std::unordered_map<size_t, MeshData>;
}