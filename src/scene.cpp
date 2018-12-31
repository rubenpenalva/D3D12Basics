#include "scene.h"

// Third party libraries
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "directxtk12/ddstextureloader_custom.h"
#include "imgui/imgui.h"

// c++ libraries
#include <fstream>
#include <algorithm>

using namespace D3D12Basics;

namespace
{
    std::wstring ExtractAssimpTextureFile(aiMaterial* material, aiTextureType type, const std::wstring& dataWorkingPath)
    {
        assert(material);

        if (material->GetTextureCount(type) > 0)
        {
            aiString textureFile;
            material->GetTexture(type, 0, &textureFile);
            return dataWorkingPath + ConvertFromUTF8ToUTF16(textureFile.C_Str());
        }

        return {};
    }

    D3D12_RESOURCE_DESC CreateSTBTextureDesc(unsigned int width, unsigned int height)
    {
        D3D12_RESOURCE_DESC resourceDesc;
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        resourceDesc.Alignment = 0;
        resourceDesc.Width = width;
        resourceDesc.Height = height;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        resourceDesc.SampleDesc = { 1, 0 };
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        return resourceDesc;
    }

    TextureData LoadSTBLoadableImage(const std::wstring& textureFileName, bool is16bit)
    {
        auto buffer = ReadFullFile(textureFileName, true);

        const stbi_uc* bufferPtr = reinterpret_cast<const stbi_uc*>(&buffer[0]);
        const int bufferLength = static_cast<int>(buffer.size());
        int textureChannelsCount;
        const int requestedChannelsCount = 4;
        int textureWidth = 0;
        int textureHeight = 0;

        void* stbiBuffer = nullptr;
        if (is16bit)
        {
            stbiBuffer = stbi_load_16_from_memory(bufferPtr, bufferLength, &textureWidth,
                                                  &textureHeight, &textureChannelsCount,
                                                  requestedChannelsCount);
        }
        else
        {
            stbiBuffer = stbi_load_from_memory(bufferPtr, bufferLength, &textureWidth,
                                               &textureHeight, &textureChannelsCount,
                                                requestedChannelsCount);
        }

        const auto textureRowSizeBytes = textureWidth * requestedChannelsCount;
        const auto textureDataSizeBytes = textureRowSizeBytes * textureHeight;
        // Copy the raw data to be able to free it later without using stbi_image_free
        auto rawData = std::make_unique<uint8_t[]>(textureDataSizeBytes);
        memcpy(rawData.get(), stbiBuffer, textureDataSizeBytes);
        stbi_image_free(stbiBuffer);

        auto resourceDesc = CreateSTBTextureDesc(textureWidth, textureHeight);
        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        subresources.push_back(D3D12_SUBRESOURCE_DATA{ rawData.get(), textureRowSizeBytes, textureDataSizeBytes });

        return TextureData{ resourceDesc, std::move(rawData), std::move(subresources) };
    }

    TextureData LoadDDSImage(const std::wstring & textureFileName)
    {
        const size_t maxsize = 0;
        const int planesCount = 1;

        std::vector<D3D12_SUBRESOURCE_DATA> subresources;
        std::unique_ptr<uint8_t[]> rawData;
        D3D12_RESOURCE_DESC resourceDesc;

        // TODO not sure about expressing out argument through a unique_ptr reference
        AssertIfFailed(DirectX::LoadDDSTextureFromFileEx_Custom(textureFileName.c_str(), maxsize,
                       rawData, subresources, planesCount, &resourceDesc));

        return TextureData{ resourceDesc, std::move(rawData), std::move(subresources) };
    }
}

EntityTransform::EntityTransform(ProjectionType projectionType)
{
    // NOTE this should not be harcoded here but good enough for this project
    if (projectionType == EntityTransform::ProjectionType::Perspective)
    {
        const float nearPlane = 0.1f;
        const float farPlane = 1000.0f;

        const float fov = M_PI_2 - M_PI_8;
        const float aspectRatio = 1.6f;
        m_localToClip = Matrix44::CreatePerspectiveFieldOfViewLH(fov, aspectRatio, nearPlane, farPlane);
    }
    else
    {
        const float nearPlane = -800.0f;
        const float farPlane = 800.0f;
        const float width = 150.0f;
        const float height = 150.0f;
        m_localToClip = Matrix44::CreateOrthographicLH(width, height, nearPlane, farPlane);
    }
    const Float3 position{};
    const Float3 target{ 0.0f, 0.0f, 1.0f };
    const Float3 up = Float3::UnitY;
    TranslateLookingAt(position, target, up);
}

void EntityTransform::TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up)
{
    m_worldToLocal = Matrix44::CreateLookAtLH(position, target, up);
 
    UpdateLocalToWorld(position);

    m_position = position;
    m_forward = -m_localToWorld.Forward();
}

void EntityTransform::UpdateLocalToWorld(const Float3& position)
{
    m_localToWorld = m_worldToLocal.Transpose();
    m_localToWorld.Translation(position);
}

SceneLoader::SceneLoader(const std::wstring& sceneFile, Scene& scene, const std::wstring& dataWorkingPath) : m_outScene(scene)
{
    if (sceneFile.empty())
        return;

    // TODO flattening the hierarchy of nodes for now
    m_assImporter.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 0x0000ffff);
    const int importFlags = aiProcess_PreTransformVertices | aiProcess_Triangulate |
                            aiProcess_CalcTangentSpace |  aiProcess_ConvertToLeftHanded | aiProcess_SplitLargeMeshes;
    auto assimpScene = m_assImporter.ReadFile(ConvertFromUTF16ToUTF8(sceneFile), importFlags);
    assert(assimpScene);
 
    m_assimpModelIdStart = scene.m_models.empty() ? 0 : static_cast<unsigned int>(scene.m_models.back().m_id) + 1;

    for (unsigned int i = 0; i < assimpScene->mNumMeshes; ++i)
    {
        auto mesh = assimpScene->mMeshes[i];

        Model model;
        model.m_name = ConvertFromUTF8ToUTF16(mesh->mName.C_Str());
        model.m_type = Model::Type::MeshFile;
        model.m_transform = D3D12Basics::Matrix44::Identity;
        model.m_normalTransform = D3D12Basics::Matrix44::Identity;
        model.m_id = m_assimpModelIdStart + i;
        model.m_uvScaleOffset = Float4{ 1.0f, 1.0f, 0.0f, 0.0f };

        auto material = assimpScene->mMaterials[mesh->mMaterialIndex];
        model.m_material.m_diffuseTexture = ExtractAssimpTextureFile(material, aiTextureType_DIFFUSE, dataWorkingPath);
        model.m_material.m_specularTexture = ExtractAssimpTextureFile(material, aiTextureType_SPECULAR, dataWorkingPath);
        model.m_material.m_normalsTexture = ExtractAssimpTextureFile(material, aiTextureType_NORMALS, dataWorkingPath);
        model.m_material.m_shadowReceiver = true;
        model.m_material.m_shadowCaster = true;

        m_outScene.m_models.push_back(std::move(model));
    }
}

TextureData SceneLoader::LoadTextureData(const std::wstring& textureFile)
{
    if (textureFile.find(L".dds") != std::wstring::npos)
    {
        return LoadDDSImage(textureFile);
    }

    const bool isHDRTexture = textureFile.find(L".hdr") != std::wstring::npos;
    return LoadSTBLoadableImage(textureFile, isHDRTexture);
}

MeshData SceneLoader::LoadMesh(size_t modelId)
{
    assert(m_assimpModelIdStart <= modelId);
    auto assimpMeshId = modelId - m_assimpModelIdStart;
    // TODO compile time assert
    assert(sizeof(aiVector3D) == sizeof(Float3));

    auto* assimpScene = m_assImporter.GetScene();
    assert(assimpScene);
    assert(assimpScene->mNumMeshes > assimpMeshId);

    auto* model = assimpScene->mMeshes[assimpMeshId];
    assert(model->HasPositions() && model->HasTextureCoords(0) && model->HasNormals() && 
           model->HasTangentsAndBitangents());
    assert(model->mNumVertices <= MeshData::m_maxVertexCount);

    // Copy the indices
    const unsigned int numIndicesPerTriangle = 3;
    std::vector<uint16_t> indices(model->mNumFaces * numIndicesPerTriangle);
    for (size_t i = 0; i < model->mNumFaces; ++i)
    {
        assert(model->mFaces[i].mNumIndices == numIndicesPerTriangle);
        for (unsigned int j = 0; j < numIndicesPerTriangle; ++j)
            indices[i * numIndicesPerTriangle + j] = static_cast<uint16_t>(model->mFaces[i].mIndices[j]);
    }
    
    const size_t positionElementsCount = 3;
    std::vector<float> positions(model->mNumVertices * positionElementsCount);
    memcpy(&positions[0], model->mVertices, sizeof(aiVector3D) * model->mNumVertices);

    const unsigned int uvElementsCount = 2;
    assert(model->mNumUVComponents[0] == uvElementsCount);
    std::vector<float> uvs(model->mNumVertices * uvElementsCount);
    for (size_t i = 0; i < model->mNumVertices; ++i)
    {
        memcpy(&uvs[i * uvElementsCount], &model->mTextureCoords[0][i], sizeof(Float2));
    }

    const size_t normalsElementsCount = 3;
    std::vector<float> normals(model->mNumVertices * normalsElementsCount);
    memcpy(&normals[0], model->mNormals, sizeof(aiVector3D) * model->mNumVertices);

    const size_t tangentElementsCount = 3;
    std::vector<float> tangents(model->mNumVertices * tangentElementsCount);
    memcpy(&tangents[0], model->mTangents, sizeof(aiVector3D) * model->mNumVertices);

    const size_t bitangentElementsCount = 3;
    std::vector<float> bitangents(model->mNumVertices * bitangentElementsCount);
    memcpy(&bitangents[0], model->mBitangents, sizeof(aiVector3D) * model->mNumVertices);

    VertexStreams streams;
    streams.AddStream(positionElementsCount, std::move(positions));
    streams.AddStream(uvElementsCount, std::move(uvs));
    streams.AddStream(normalsElementsCount, std::move(normals));
    streams.AddStream(tangentElementsCount, std::move(tangents));
    streams.AddStream(bitangentElementsCount, std::move(bitangents));
    const auto vertexElementsCount = streams.VertexElementsCount();

    return MeshData{ streams.GetStreams(), std::move(indices), model->mNumVertices, vertexElementsCount * sizeof(float), vertexElementsCount };
}

CameraController::CameraController()
{
}

void CameraController::Update(EntityTransform& camera, float deltaTime, float totalTime)
{
    ProcessInput();

    UpdateCamera(camera, deltaTime, totalTime);
}

void CameraController::ProcessMouseInput()
{
    if (!ImGui::IsMouseDragging(0))
        return;

    auto mouseDrag = ImGui::GetMouseDragDelta();
    Float3 direction = Float3(static_cast<float>(mouseDrag.x), static_cast<float>(-mouseDrag.y), 0.0f);
    m_cameraState.m_speedLookModifier = 0.5f;
    direction.Normalize();

    m_cameraState.m_target = direction;
}

void CameraController::ProcessKeyboardInput()
{
    if (ImGui::IsKeyPressed(VK_RETURN))
        m_cameraState.m_manualMovement = !m_cameraState.m_manualMovement;

    if (ImGui::IsKeyDown(VK_OEM_MINUS))
        m_cameraState.m_maxSpeed -= 0.5f;
    if (ImGui::IsKeyDown(VK_OEM_PLUS))
        m_cameraState.m_maxSpeed += 0.5f;

    bool keyPressed = false;
    if (ImGui::IsKeyDown('W'))
    {
        keyPressed = true;
        m_cameraState.m_direction = Float3(0.0f, 0.0f, 1.0f);
    }
    if (ImGui::IsKeyDown('S'))
    {
        keyPressed = true;
        m_cameraState.m_direction = Float3(0.0f, 0.0f, -1.0f);
    }
    if (ImGui::IsKeyDown('A'))
    {
        keyPressed = true;
        m_cameraState.m_direction = Float3(-1.0f, 0.0f, 0.0f);
    }
    if (ImGui::IsKeyDown('D'))
    {
        keyPressed = true;
        m_cameraState.m_direction = Float3(1.0f, 0.0f, 0.0f);
    }
    if (keyPressed)
        m_cameraState.m_speedModifier = 1.0f;
}

void CameraController::ProcessInput()
{
    m_cameraState.m_speedModifier = 0.0f;
    m_cameraState.m_speedLookModifier = 0.0f;
    if (m_cameraState.m_maxSpeed < 0.0f)
        m_cameraState.m_maxSpeed = 0.0f;

    ProcessKeyboardInput();

    ProcessMouseInput();
}

void CameraController::UpdateCamera(EntityTransform& camera, float deltaTime, float totalTime)
{
    if (m_cameraState.m_manualMovement)
    {
        Float3 cameraPos = camera.Position();

        if (m_cameraState.m_speedModifier != 0.0f)
        {
            cameraPos += Float3::TransformNormal(m_cameraState.m_direction, 
                                                 camera.LocalToWorld()) *
                                                 deltaTime * m_cameraState.m_speedModifier *
                                                 m_cameraState.m_maxSpeed;
        }

        Float3 cameraTarget = cameraPos + camera.Forward();

        if (m_cameraState.m_speedLookModifier != 0.0f)
        {
            cameraTarget += Float3::TransformNormal(m_cameraState.m_target, camera.LocalToWorld()) *
                                                    deltaTime * m_cameraState.m_speedLookModifier *
                                                    m_cameraState.m_maxLookSpeed;
        }

        camera.TranslateLookingAt(cameraPos, cameraTarget);
    }
    else
    {
        Float3 cameraPos;
        Float3 cameraTarget = D3D12Basics::Float3::Zero;

        const float longitude = 2.f * (1.0f / D3D12Basics::M_2PI) * totalTime;
        const float latitude = D3D12Basics::M_PI_4 + D3D12Basics::M_PI_8;
        const float altitude = 25.0f;
        cameraPos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude);

        camera.TranslateLookingAt(cameraPos, cameraTarget);
    }
}

AppController::AppController()
{
}

void AppController::Update(CustomWindow& customWindow, bool& quit)
{
    quit = false;

    if (ProcessKeyboardInput(customWindow))
    {
        quit = true;
        return;
    }
}

bool AppController::ProcessKeyboardInput(D3D12Basics::CustomWindow& customWindow)
{
    if (ImGui::IsKeyPressed(VK_ESCAPE))
        return true;

    if (ImGui::IsKeyPressed(VK_SPACE))
        customWindow.ChangeFullscreenMode();

    return false;
}