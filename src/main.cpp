// c++ includes
#include <sstream>

// windows includes
#include <windows.h>

// Project includes
#include "d3d12backendrender.h"

using namespace D3D12Basics;
using namespace D3D12Render;

namespace
{
    // NOTE:  Assuming working directory contains the data folder
    const wchar_t* g_texture256FileName    = L"./data/texture_256.png";
    const wchar_t* g_texture1024FileName   = L"./data/texture_1024.jpg";

    const size_t g_planesCount  = 1;
    const size_t g_planeModelID = 0;

    const size_t g_spheresCount = 100;
    const size_t g_spheresModelStartID = g_planeModelID + g_planesCount;

    const size_t g_cubesCount = 25;
    const size_t g_cubesModelStartID = g_spheresModelStartID + g_spheresCount;

    const size_t g_modelsCount = g_planesCount + g_spheresCount + g_cubesCount;

    bool HandleWindowMessages()
    {
        MSG msg = {};
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                return true;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        return false;
    }

    Scene CreateScene()
    {
        const auto cameraPosition = D3D12Basics::Float3(0.0f, 0.0f, -5.0f);
        Scene scene { cameraPosition };
        
        scene.m_models.resize(g_modelsCount);

        // Plane
        {
            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(10.0f) * D3D12Basics::Matrix44::CreateRotationX(D3D12Basics::M_PI_2);
            D3D12Basics::Mesh mesh = CreatePlane();
            Model plane {L"Ground plane", localToWorld, mesh, g_texture256FileName };

            scene.m_models[g_planeModelID] = plane;
        }

        // Spheres
        const float spheresAngleDiff = (D3D12Basics::M_2PI / g_spheresCount);
        for (size_t i = 0; i < g_spheresCount; ++i)
        {
            const float longitude = spheresAngleDiff * i;
            const float latitude = D3D12Basics::M_PI_2;
            const float altitude = 3.3f;
            D3D12Basics::Float3 spherePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + D3D12Basics::Float3(0.0f, 0.3f, 0.0f);

            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(0.2f) * D3D12Basics::Matrix44::CreateTranslation(spherePos);
            D3D12Basics::Mesh mesh = CreateSphere(20, 20);
            std::wstringstream converter;
            converter << L"Sphere " << i;
            Model sphere { converter.str().c_str(), localToWorld, mesh, g_texture1024FileName };

            scene.m_models[g_spheresModelStartID + i] = sphere;
        }
        
        // Cubes
        const float cubesAngleDiff = (D3D12Basics::M_2PI / g_cubesCount);
        for (size_t i = 0; i < g_cubesCount; ++i)
        {
            const float longitude = cubesAngleDiff * i;
            const float latitude = D3D12Basics::M_PI_2;
            const float altitude = 2.0f;
            const auto cubeOffsetPos = D3D12Basics::Float3(0.0f, (sinf(static_cast<float>(i)) * 0.5f + 0.5f) * 0.5f + 0.35f, 0.0f);
            D3D12Basics::Float3 cubePos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude) + cubeOffsetPos;

            D3D12Basics::Matrix44 localToWorld = D3D12Basics::Matrix44::CreateScale(0.35f) * D3D12Basics::Matrix44::CreateTranslation(cubePos);
            D3D12Basics::Mesh mesh = CreateCube();
            std::wstringstream converter;
            converter << L"Cube " << i;
            Model cube{ converter.str().c_str(), localToWorld, mesh, g_texture1024FileName };

            scene.m_models[g_cubesModelStartID + i] = cube;
        }

        return scene;
    }

    void UpdateScene(Scene& scene, float accumulatedTime)
    {
        const float longitude = (1.0f / D3D12Basics::M_2PI) * accumulatedTime;
        const float latitude = D3D12Basics::M_PI_4 + D3D12Basics::M_PI_8;
        const float altitude = 5.0f;
        D3D12Basics::Float3 cameraPos = D3D12Basics::SphericalToCartersian(longitude, latitude, altitude);

        scene.m_camera.TranslateLookingAt(cameraPos, D3D12Basics::Float3::Zero);
    }
}

int WINAPI WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*szCmdLine*/, int /*iCmdShow*/)
{
    Scene scene = CreateScene();

    D3D12BackendRender backendRender(scene);

    D3D12Basics::CustomWindow customWindow(backendRender.GetSafestResolutionSupported());

    backendRender.SetOutputWindow(customWindow.GetHWND());

    backendRender.LoadSceneResources();

    Timer timer;

    // Game loop
    bool quit = false;
    while (!quit)
    {
        quit = HandleWindowMessages();

        // Process the wndproc events state
        {
            if (customWindow.HasFullscreenChanged())
                backendRender.OnToggleFullScreen();

            if (customWindow.HasResolutionChanged())
                backendRender.OnResize(customWindow.GetResolution());

            customWindow.ResetWndProcEventsState();
        }

        // Kick the work: update scene - update backend resources - render frame - present frame
        UpdateScene(scene, timer.TotalTime());

        backendRender.UpdateSceneResources();

        backendRender.RenderFrame();

        backendRender.FinishFrame();

        // Measure elapsed time
        timer.Mark();
    }

    return 0;
}