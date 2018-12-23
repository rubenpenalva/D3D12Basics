// c++ includes
#include <sstream>
#include <iostream>
#include <filesystem>

// windows includes
#include <windows.h>

// Project includes
#include "d3d12basicsengine.h"

// thirdparty libraries include
#include "imgui/imgui.h"

#include <fstream>

using namespace D3D12Basics;

// NOTE: horrible but good enough for this project as scene management is not a feature
#define LOAD_ONLY_PLANE         (0)
#define LOAD_ONLY_AXIS_GUIZMOS  (0)
#define LOAD_ENABLED (!LOAD_ONLY_PLANE && !LOAD_ONLY_AXIS_GUIZMOS)

#define LOAD_AXIS_GUIZMOS   (1 && LOAD_ENABLED)
#define LOAD_SPHERES        (1 && LOAD_ENABLED)
#define LOAD_CUBES          (1 && LOAD_ENABLED)
#define LOAD_PLANE          (1 && LOAD_ENABLED)
#define LOAD_SPONZA         (1 && LOAD_ENABLED)
#define LOAD_WAVE           (1 && LOAD_ENABLED)

namespace
{
    // NOTE: Assuming working directory contains the data folder
    const wchar_t* g_sponzaDataWorkingPath = L"./data/sponza/";
    const wchar_t* g_sponzaModel = L"./data/sponza/sponza.dae";

    const wchar_t* g_texture256FileName     = L"./data/texture_256.png";
    const wchar_t* g_texture1024FileName    = L"./data/texture_1024.jpg";

#if (LOAD_PLANE || LOAD_ONLY_PLANE)
    const size_t g_planesCount          = 1;
#else
    const size_t g_planesCount          = 0;
#endif
    const size_t g_planeModelID         = 0;

#if LOAD_SPHERES 
#if LOAD_AXIS_GUIZMOS
    const size_t g_spheresCount         = 31;
    const float g_spheresAngleDiff      = (D3D12Basics::M_2PI / (g_spheresCount - 1));
#else
    const size_t g_spheresCount         = 30;
    const float g_spheresAngleDiff      = (D3D12Basics::M_2PI / g_spheresCount);
#endif // !LOAD_AXIS_GUIZMOS
#else
#if LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS
    const size_t g_spheresCount         = 1;
#else
    const size_t g_spheresCount         = 0;
#endif // !(LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS)
#endif // LOAD_SPHERES
    const size_t g_spheresModelStartID  = g_planeModelID + g_planesCount;

#if LOAD_CUBES
#if LOAD_AXIS_GUIZMOS
    const size_t g_cubesCount           = 21;
    const float g_cubesAngleDiff        = (D3D12Basics::M_2PI / (g_cubesCount - 1));
#else
    const size_t g_cubesCount           = 20;
    const float g_cubesAngleDiff        = (D3D12Basics::M_2PI / g_cubesCount);
#endif // !LOAD_AXIS_GUIZMOS
#else
#if LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS
    const size_t g_cubesCount           = 1;
#else
    const size_t g_cubesCount           = 0;
#endif // !(LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS)
#endif // LOAD_CUBES
    const size_t g_cubesModelStartID    = g_spheresModelStartID + g_spheresCount;

#if LOAD_WAVE
    const size_t g_waveColsCount        = 30; // width
    const size_t g_waveRowsCount        = 20; // depth
    const size_t g_waveEntsCount        = g_waveColsCount * g_waveRowsCount;
    const float g_waveWidth             = 150.0f;
    const float g_waveDepth             = 50.0f;
    const float g_waveHeight            = 20.0f;
    const float g_waveHalfWidth         = g_waveWidth * 0.5f;
    const float g_waveHalfDepth         = g_waveDepth * 0.5f;
    const float g_waveCellWidth         = (g_waveWidth / g_waveColsCount);
    const float g_waveCellDepth         = (g_waveDepth / g_waveRowsCount);
    const float g_waveEntSizeScale      = 0.1f;
    const float g_waveEntSize           = g_waveCellWidth * g_waveEntSizeScale;
    const float g_waveCellWidthOffset   = g_waveCellWidth * (1.0f - g_waveEntSizeScale) * 0.5f;
    const float g_waveCellDepthOffset   = g_waveCellDepth * (1.0f - g_waveEntSizeScale) * 0.5f;
#else
    const size_t g_waveEntsCount    = 0;
#endif
    const size_t g_waveEntsModelStartID = g_cubesModelStartID + g_cubesCount;

    const size_t g_modelsCount = g_planesCount + g_spheresCount + g_cubesCount + g_waveEntsCount;

    const Float3 g_modelsOffset = { -30.0f, 0.0f, 0.0 };

    const wchar_t* g_enableWaitForPresentCmdName        = L"waitForPresent";
    const size_t g_enableWaitForPresentCmdNameLength    = wcslen(g_enableWaitForPresentCmdName);

    struct CommandLine
    {
        bool m_isWaitableForPresentEnabled;
    };

#if LOAD_SPHERES
    D3D12Basics::Matrix44 CalculateSphereLocalToWorld(size_t sphereID, float totalTime)
    {
        const float longitude = g_spheresAngleDiff * sphereID;
        const float latitude = D3D12Basics::M_PI_2;
        const float altitude = 15.0f;
        const auto sphereOffsetPos = D3D12Basics::Float3(0.0f, 2.0f + (sinf(sphereID - totalTime * 5.0f) * 0.5f + 0.5f)*0.5f, 0.0f) + g_modelsOffset;
        D3D12Basics::Float3 spherePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + sphereOffsetPos;

        return D3D12Basics::Matrix44::CreateScale(2.0f) * D3D12Basics::Matrix44::CreateTranslation(spherePos);
    }
#endif // LOAD_SPHERES

    Scene CreateScene()
    {
        Scene scene;
#if LOAD_ONLY_PLANE || LOAD_PLANE || LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS || LOAD_SPHERES || LOAD_CUBES || LOAD_WAVE
        if (!g_modelsCount)
            return scene;

        std::vector<Model> models(g_modelsCount);
        size_t modelId = 0;
#endif

#if LOAD_ONLY_PLANE || LOAD_PLANE
        // Plane
        {
            D3D12Basics::Matrix44 localToWorld =    D3D12Basics::Matrix44::CreateScale(150.0f, 50.0f, 1.0f) *
                                                    D3D12Basics::Matrix44::CreateRotationX(D3D12Basics::M_PI_2);
            D3D12Basics::Matrix44 normalLocalToWorld = D3D12Basics::Matrix44::CreateRotationX(D3D12Basics::M_PI_2);

            Material material;
            material.m_diffuseTexture = g_texture256FileName;
            material.m_shadowReceiver = true;
            material.m_shadowCaster = true;

            models[g_planeModelID] = Model{ L"Ground plane", Model::Type::Plane, 
                                            modelId++, Float4{6.0f, 2.0f, 0.0f, 0.0f}, 
                                            localToWorld, normalLocalToWorld, material };
        }
#endif // LOAD_ONLY_PLANE || LOAD_PLANE

#if LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS
        {
            Material fixedColorMat;
            fixedColorMat.m_diffuseColor = Float3 { 1.0f, 0.0f, 0.0f };
            fixedColorMat.m_shadowReceiver = false;
            fixedColorMat.m_shadowCaster = false;
            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(6.0f, 0.0f, 0.0f);
            D3D12Basics::Matrix44 normalLocalToWorld;

            models[g_spheresModelStartID] = Model{  L"Sphere +X", Model::Type::Sphere,
                                                    modelId++, Float4{1.0f, 1.0f, 0.0f, 0.0f}, 
                                                    localToWorld, normalLocalToWorld,
                                                    fixedColorMat };
        }
        {
            Material fixedColorMat;
            fixedColorMat.m_diffuseColor = Float3{ 0.0f, 0.0f, 1.0f };
            fixedColorMat.m_shadowReceiver = false;
            fixedColorMat.m_shadowCaster = false;

            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateTranslation(0.0f, 0.0f, 6.0f);
            D3D12Basics::Matrix44 normalLocalToWorld;

            models[g_cubesModelStartID] = Model{    L"Cube +Z", Model::Type::Cube,
                                                    modelId++, Float4{1.0f, 1.0f, 0.0f, 0.0f}, 
                                                    localToWorld, normalLocalToWorld,
                                                    fixedColorMat };
        }
#endif // LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS
        Material material; 
        material.m_diffuseTexture = g_texture1024FileName;
        material.m_shadowReceiver = true;
        material.m_shadowCaster = true;

#if LOAD_SPHERES
#if LOAD_AXIS_GUIZMOS
        const size_t sphereIDOffset = 1;
        const size_t spheresAxisOffsetStart = 1;
#else
        const size_t sphereIDOffset = 0;
        const size_t spheresAxisOffsetStart = 0;
#endif // !LOAD_AXIS_GUIZMOS
        // Spheres
        for (size_t i = spheresAxisOffsetStart; i < g_spheresCount; ++i)
        {
            D3D12Basics::Matrix44 localToWorld = CalculateSphereLocalToWorld(i - sphereIDOffset, 0.0f);
            D3D12Basics::Matrix44 normalLocalToWorld;

            std::wstringstream converter;

            converter << "Sphere " << i;

            models[g_spheresModelStartID + i] = Model{  converter.str().c_str(), Model::Type::Sphere, 
                                                        modelId++, Float4{1.0f, 1.0f, 0.0f, 0.0f}, 
                                                        localToWorld, normalLocalToWorld,
                                                        material };
        }
#endif // LOAD_SPHERES
#if LOAD_CUBES
#if LOAD_AXIS_GUIZMOS
        const size_t cubeIDOffset = 1;
        const size_t cubesAxisOffsetStart = 1;
#else
        const size_t cubeIDOffset = 0;
        const size_t cubesAxisOffsetStart = 0;
#endif // !LOAD_AXIS_GUIZMOS
        // Cubes
        for (size_t i = cubesAxisOffsetStart; i < g_cubesCount; ++i)
        {
            const size_t cubeId = i - cubeIDOffset;
            const float longitude = g_cubesAngleDiff * cubeId;
            const float latitude = D3D12Basics::M_PI_2;
            const float altitude = 10.0f;
            const auto cubeOffsetPos = D3D12Basics::Float3(0.0f, 1.75f + (sinf(static_cast<float>(cubeId)) * 0.5f + 0.5f) * 0.5f + 0.5f, 0.0f);
            D3D12Basics::Float3 cubePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + cubeOffsetPos;

            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(1.5f) * D3D12Basics::Matrix44::CreateTranslation(cubePos);
            D3D12Basics::Matrix44 normalLocalToWorld;

            std::wstringstream converter;
            converter << "Cube " << i;

            models[g_cubesModelStartID + i] = Model {   converter.str().c_str(), Model::Type::Cube, 
                                                        modelId++, Float4{1.0f, 1.0f, 0.0f, 0.0f},
                                                        localToWorld, normalLocalToWorld,
                                                        material};
        }
#endif // LOAD_CUBES

#if LOAD_WAVE
        assert(g_waveColsCount > g_waveRowsCount);

        const float y = g_waveHeight;

        for (size_t i = 0; i < g_waveColsCount; ++i)
        {
            const float x = -g_waveHalfWidth + i * g_waveCellWidth + g_waveCellWidthOffset;
            for (size_t j = 0; j < g_waveRowsCount; ++j)
            {
                const float z = -g_waveHalfDepth + j * g_waveCellDepth + g_waveCellDepthOffset;

                const size_t cellIndex = i * g_waveRowsCount + j;

                D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(g_waveEntSize) * 
                                                     D3D12Basics::Matrix44::CreateTranslation(x, y, z);
                D3D12Basics::Matrix44 normalLocalToWorld;

                std::wstringstream converter;

                converter << "Wave Entity " << cellIndex;

                models[g_waveEntsModelStartID + cellIndex] = Model{ converter.str().c_str(), Model::Type::Sphere,
                                                                    modelId++, Float4{1.0f, 1.0f, 0.0f, 0.0f},
                                                                    localToWorld, normalLocalToWorld,
                                                                    material };
            }
        }
#endif
#if LOAD_SPONZA
        scene.m_sceneFile = g_sponzaModel;
#endif
#if LOAD_ONLY_PLANE || LOAD_PLANE || LOAD_AXIS_GUIZMOS || LOAD_ONLY_AXIS_GUIZMOS || LOAD_SPHERES || LOAD_CUBES || LOAD_WAVE
        scene.m_models = std::move(models);
#endif

        scene.m_lights.emplace_back(EntityTransform::ProjectionType::Orthographic, 10.0f);
        scene.m_lights[0].m_transform.TranslateLookingAt({ 0.0f, 1.0f, 0.0f }, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
        scene.m_lights.emplace_back(EntityTransform::ProjectionType::Orthographic, 10.0f );
        scene.m_lights[1].m_transform.TranslateLookingAt({ 0.0f, 1.0f, 0.0f }, { -0.85f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });

        return scene;
    }

    void UpdateScene(Scene& scene, float totalTime)
    {
        scene;
        totalTime;

#if LOAD_SPHERES
#if LOAD_AXIS_GUIZMOS
        const size_t spheresAxisOffsetStart = 1;
#else
        const size_t spheresAxisOffsetStart = 0;
#endif // !LOAD_AXIS_GUIZMOS
        // Spheres
        for (size_t i = spheresAxisOffsetStart; i < g_spheresCount; ++i)
        {
            auto& sphereModel = scene.m_models[g_spheresModelStartID + i];

            sphereModel.m_transform = CalculateSphereLocalToWorld(i, totalTime);
        }
#endif // LOAD_SPHERES
#if LOAD_WAVE
        for (size_t i = 0; i < g_waveColsCount; ++i)
        {
            const float x = -g_waveHalfWidth + i * g_waveCellWidth + g_waveCellWidthOffset;
            const float y = g_waveHeight + 2.0f * (sinf(x - totalTime));

            for (size_t j = 0; j < g_waveRowsCount; ++j)
            {
                const float z = -g_waveHalfDepth + j * g_waveCellDepth + g_waveCellDepthOffset;
                const size_t cellIndex = i * g_waveRowsCount + j;

                auto& waveModel = scene.m_models[g_waveEntsModelStartID + cellIndex];
                waveModel.m_transform = D3D12Basics::Matrix44::CreateScale(g_waveEntSize) *
                                        D3D12Basics::Matrix44::CreateTranslation(x, y, z);
            }
        }
#endif // LOAD_WAVE
    }

    CommandLine ProcessCmndLine(LPWSTR szCmdLine)
    {
        CommandLine cmdLine{};

        if (szCmdLine[0] == '-')
        {
            size_t w = 0;
            for (size_t j = 1;
                szCmdLine[j] != '\0' && szCmdLine[j] != '-' &&
                j < g_enableWaitForPresentCmdNameLength &&
                szCmdLine[j] == g_enableWaitForPresentCmdName[w];
                ++j, ++w);

            cmdLine.m_isWaitableForPresentEnabled = w == g_enableWaitForPresentCmdNameLength - 1;
        }

        return cmdLine;
    }
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPWSTR szCmdLine, int /*iCmdShow*/)
{
    auto cmdLine = ProcessCmndLine(szCmdLine);
    D3D12Basics::D3D12BasicsEngine::Settings settings{ cmdLine.m_isWaitableForPresentEnabled, g_sponzaDataWorkingPath };

	// Note CreateScene will create the scene description but wont load any resources.
    D3D12Basics::D3D12BasicsEngine d3d12Engine(settings, CreateScene());

    // Game loop
    MSG msg = {};
    while (msg.message != WM_QUIT && !d3d12Engine.HasUserRequestedToQuit())
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            d3d12Engine.BeginFrame();

            d3d12Engine.RunFrame(UpdateScene);

            d3d12Engine.EndFrame();
        }
    }

    return 0;
}