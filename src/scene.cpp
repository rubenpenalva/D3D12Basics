#include "scene.h"

// Third party libraries
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "assimp/postprocess.h"
#include "assimp/scene.h"
#include "directxtk12/ddstextureloader_custom.h"

// c++ libraries
#include <fstream>
#include <algorithm>

using namespace D3D12Basics;

const Float3 Material::m_diffuseColor = { 1.0f, 0.0f, 1.0f };

namespace
{
    std::optional<std::wstring> ExtractAssimpTextureFile(aiMaterial* material, 
                                                         aiTextureType type, const std::wstring& dataWorkingPath)
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

    TextureData LoadSTBLoadableImage(const std::wstring& textureFileName)
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
        int textureWidth = 0;
        int textureHeight = 0;
        stbi_uc* stbiBuffer = stbi_load_from_memory(bufferPtr, bufferLength, &textureWidth,
                                                    &textureHeight, &textureChannelsCount,
                                                    requestedChannelsCount);
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

Camera::Camera() 
{
    const Float3 position{};
    const Float3 target{ 0.0f, 0.0f, 1.0f };
    const float fov = M_PI_2 - M_PI_8;
    const float aspectRatio = 1.6f;
    const float nearPlane = 0.1f;
    const float farPlane = 1000.0f;
    const Float3 up = Float3::UnitY;

    m_cameraToClip = Matrix44::CreatePerspectiveFieldOfViewLH(fov, aspectRatio, nearPlane, farPlane);

    TranslateLookingAt(position, target, up);
}

void Camera::TranslateLookingAt(const Float3& position, const Float3& target, const Float3& up)
{
    m_worldToCamera = Matrix44::CreateLookAtLH(position, target, up);
    
    UpdateCameraToWorld(position);

    m_position = position;
    m_forward = -m_cameraToWorld.Forward();
}

void Camera::UpdateCameraToWorld(const Float3& position)
{
    m_cameraToWorld = m_worldToCamera.Transpose();
    m_cameraToWorld.Translation(position);
}

SceneLoader::SceneLoader(const std::wstring& sceneFile, Scene& scene, const std::wstring& dataWorkingPath) : m_outScene(scene)
{
    // TODO flattening the hierarchy of nodes for now
    m_assImporter.SetPropertyInteger(AI_CONFIG_PP_SLM_VERTEX_LIMIT, 0x0000ffff);
    const int importFlags = aiProcess_PreTransformVertices | aiProcess_Triangulate | aiProcess_GenNormals | 
                            aiProcess_ConvertToLeftHanded | aiProcess_SplitLargeMeshes;
    auto assimpScene = m_assImporter.ReadFile(ConvertFromUTF16ToUTF8(sceneFile), importFlags);
    assert(assimpScene);

    for (unsigned int i = 0; i < assimpScene->mNumMeshes; ++i)
    {
        auto mesh = assimpScene->mMeshes[i];

        Model model;
        model.m_name = ConvertFromUTF8ToUTF16(mesh->mName.C_Str());
        model.m_type = Model::Type::MeshFile;
        model.m_transform = D3D12Basics::Matrix44::Identity;
        model.m_id = i;

        auto material = assimpScene->mMaterials[mesh->mMaterialIndex];
        model.m_material.m_diffuseTexture = ExtractAssimpTextureFile(material, aiTextureType_DIFFUSE, dataWorkingPath);
        model.m_material.m_specularTexture = ExtractAssimpTextureFile(material, aiTextureType_SPECULAR, dataWorkingPath);
        model.m_material.m_normalsTexture = ExtractAssimpTextureFile(material, aiTextureType_NORMALS, dataWorkingPath);

        m_outScene.m_models.push_back(std::move(model));
    }
}

TextureData SceneLoader::LoadTextureData(const std::wstring& textureFile)
{
    if (textureFile.find(L".dds") != std::wstring::npos)
    {
        return LoadDDSImage(textureFile);
    }

    return LoadSTBLoadableImage(textureFile);
}

Mesh SceneLoader::LoadMesh(size_t modelId)
{
    // TODO compile time assert
    assert(sizeof(aiVector3D) == sizeof(Float3));

    auto* assimpScene = m_assImporter.GetScene();
    assert(assimpScene);
    assert(assimpScene->mNumMeshes > modelId);

    auto* model = assimpScene->mMeshes[modelId];
    assert(model->HasPositions() && model->HasTextureCoords(0));
    assert(model->mNumVertices <= Mesh::m_maxVertexCount);

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

    VertexStreams streams;
    streams.AddStream(positionElementsCount, std::move(positions));
    streams.AddStream(uvElementsCount, std::move(uvs));
    const auto vertexElementsCount = streams.VertexElementsCount();

    return Mesh{ streams.GetStreams(), std::move(indices), model->mNumVertices, vertexElementsCount * sizeof(float), vertexElementsCount };
}