#include "utils.h"

// C includes
#include <cassert>

// C++ includes
#include <string>
#include <iostream>
#include <sstream>

// windows includes
#include <windows.h>

using namespace D3D12Basics;

D3D12Basics::Resolution g_resolution { 1280, 720 , 1280.0f / 720.0f };

namespace
{
    const std::wstring  g_className{ L"MainWindowClass" };

    Resolution ClientToWindowResolution(DWORD style, const Resolution& clientResolution)
    {
        RECT rect = { 0, 0, static_cast<LONG>(clientResolution.m_width), static_cast<LONG>(clientResolution.m_height) };
        BOOL result = AdjustWindowRect(&rect, style, FALSE);
#if NDEBUG
        result;
#endif
        assert(result);
        const unsigned int  windowWidth = rect.right - rect.left;
        const unsigned int windowHeight = rect.bottom - rect.top;

        return Resolution{ windowWidth, windowHeight };
    }

    // The default window proc doesnt handle the destroy message....
    LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        switch (message)
        {
        // https://msdn.microsoft.com/en-us/library/windows/desktop/ff381396(v=vs.85).aspx
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            break;

        default:
            break;
        }

        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    HWND CreateCustomWindow()
    {
        const std::wstring name = L"MainWindow";
        const DWORD windowedStyle = WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX ^ WS_MINIMIZEBOX | WS_VISIBLE;
        const DWORD borderlessStyle = WS_POPUP | WS_VISIBLE;

        Resolution windowResolution = ClientToWindowResolution(windowedStyle, g_resolution);

        /// Initialize window class
        WNDCLASSEX wc;
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = 0;
        wc.lpfnWndProc = WndProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = GetModuleHandle(0);
        wc.hIcon = LoadIcon(0, IDI_APPLICATION);
        wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszMenuName = 0;
        wc.lpszClassName = g_className.c_str();
        wc.hIconSm = LoadIcon(0, IDI_APPLICATION);

        /// Register window class
        {
            HRESULT result = RegisterClassEx(&wc);
#if NDEBUG
            result;
#endif
            assert(SUCCEEDED(result));
        }

        /// Create Window
        HWND hwnd = CreateWindowEx(0, g_className.c_str(), name.c_str(), windowedStyle, 0, 0, windowResolution.m_width, windowResolution.m_height, nullptr, 0, wc.hInstance, nullptr);
        assert(hwnd);

        return hwnd;
    }
}

void D3D12Basics::AssertIfFailed(HRESULT hr)
{
#if NDEBUG
    hr;
#endif
    assert(SUCCEEDED(hr));
}

// TODO move to utils
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

Resolution CustomWindow::GetResolution()
{
    return g_resolution;
}

CustomWindow::CustomWindow() : m_hwnd(CreateCustomWindow())
{
    ::SwitchToThisWindow(m_hwnd, TRUE);
}

CustomWindow::~CustomWindow()
{
    UnregisterClass(g_className.c_str(), GetModuleHandle(0));
}


