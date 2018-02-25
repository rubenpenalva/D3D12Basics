#include "utils.h"

// C includes
#include <cassert>

// C++ includes
#include <string>
#include <iostream>

// windows includes
#include <windows.h>

using namespace D3D12Basics;

namespace
{
    const std::wstring  g_className{ L"MainWindowClass" };
    const DWORD g_windowedStyle = WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_MINIMIZEBOX);
    const DWORD g_fullscreenStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME);

    // The default window proc doesnt handle the destroy message....
    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        CustomWindow* customWindow = reinterpret_cast<CustomWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_CREATE:
        {
            // Save the CustomWindow* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lparam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

        // https://msdn.microsoft.com/en-us/library/windows/desktop/ff381396(v=vs.85).aspx
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_KEYDOWN:
        {
            const uint8_t key = static_cast<UINT8>(wparam);
            if (key == VK_ESCAPE)
            {
                PostQuitMessage(0);
                return 0;
            }
            else if (key == VK_SPACE)
            {
                customWindow->ChangeFullscreenMode();
                return 0;
            }

        }
            break;
        
        // Handle beep sound
        case WM_MENUCHAR:
            if (wparam & VK_RETURN)
                return MAKELRESULT(0, MNC_CLOSE);

        case WM_SIZE:
        {
            RECT windowRect = {};
            GetClientRect(hwnd, &windowRect);
            customWindow->ChangeResolution(windowRect);
        }

        return 0;

        default:
            break;
        }

        return DefWindowProc(hwnd, message, wparam, lparam);
    }

}

void D3D12Basics::AssertIfFailed(HRESULT hr)
{
#if NDEBUG
    hr;
#endif
    assert(SUCCEEDED(hr));
}

Float3 D3D12Basics::SphericalToCartersian(float longitude, float latitude, float altitude)
{
    const float sinLat = sinf(latitude);
    const float sinLon = sinf(longitude);

    const float cosLat = cosf(latitude);
    const float cosLon = cosf(longitude);

    Float3 cartesianCoordinates;
    cartesianCoordinates.x = sinLat * cosLon;
    cartesianCoordinates.y = cosLat;
    cartesianCoordinates.z = sinLat * sinLon;
    return cartesianCoordinates * altitude;
}

CustomWindow::CustomWindow(const Resolution& resolution)    :   m_resolutionChanged(false), 
                                                                m_currentResolution(resolution), 
                                                                m_fullscreenChanged(false)
{
    CreateCustomWindow();

    ::SwitchToThisWindow(m_hwnd, TRUE);
}

CustomWindow::~CustomWindow()
{
    UnregisterClass(g_className.c_str(), GetModuleHandle(0));
}

void CustomWindow::ChangeResolution(const RECT& windowRect)
{ 
    Resolution resolution
    {
        static_cast<unsigned int>(windowRect.right - windowRect.left), 
        static_cast<unsigned int>(windowRect.bottom - windowRect.top) 
    };

    if ((m_currentResolution.m_width == resolution.m_width) &&
        (m_currentResolution.m_height == resolution.m_height))
    {
        return;
    }
    
    m_resolutionChanged = true;
    m_currentResolution = resolution;
}

void CustomWindow::ResetWndProcEventsState()
{
    m_resolutionChanged = false;
    m_fullscreenChanged = false;
}

void CustomWindow::CreateCustomWindow()
{
    const std::wstring name = L"MainWindow";

    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_className.c_str();
    wc.hIconSm = LoadIcon(0, IDI_APPLICATION);

    // Register window class
    AssertIfFailed(RegisterClassEx(&wc));

    // Create Window
    DEVMODE devMode = {};
    devMode.dmSize = sizeof(DEVMODE);
    EnumDisplaySettings(nullptr, ENUM_CURRENT_SETTINGS, &devMode);
    unsigned int positionX = (devMode.dmPelsWidth / 2) - (m_currentResolution.m_width / 2);
    unsigned int positionY = (devMode.dmPelsHeight / 2) - (m_currentResolution.m_height / 2);

    m_hwnd = CreateWindow(g_className.c_str(), name.c_str(), g_windowedStyle,
                          positionX, positionY,
                          m_currentResolution.m_width, m_currentResolution.m_height,
                          nullptr, nullptr, wc.hInstance, this);
    assert(m_hwnd);

    ShowWindow(m_hwnd, SW_SHOW);
}

Timer::Timer() : m_elapsedTime(0.0f), m_totalTime(0.0f)
{
    m_mark = std::chrono::high_resolution_clock::now();
}

void Timer::Mark()
{
    const auto lastMark = m_mark;
    m_mark = std::chrono::high_resolution_clock::now();

    m_elapsedTime = std::chrono::duration<float>(m_mark - lastMark).count();
    m_totalTime += m_elapsedTime;
}