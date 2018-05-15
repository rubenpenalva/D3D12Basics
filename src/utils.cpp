#include "utils.h"

// C includes
#include <cassert>

// C++ includes
#include <string>
#include <iostream>

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

        case WM_ACTIVATEAPP:
            DirectX::Keyboard::ProcessMessage(message, wparam, lparam);
            DirectX::Mouse::ProcessMessage(message, wparam, lparam);
            break;
        case WM_INPUT:
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MOUSEWHEEL:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_MOUSEHOVER:
            DirectX::Mouse::ProcessMessage(message, wparam, lparam);
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            DirectX::Keyboard::ProcessMessage(message, wparam, lparam);
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
        break;

        default:
            return DefWindowProc(hwnd, message, wparam, lparam);
        }

        return 0;
    }

}

VertexStreams::VertexStreams() : m_vertexElementsCount(0)
{
}

void VertexStreams::AddStream(size_t elementsCount, std::vector<float>&& data)
{
    assert(elementsCount);
    assert(data.size());

    VertexStream stream;
    stream.m_elementsCount = elementsCount;
    stream.m_elementOffset = m_vertexElementsCount;
    stream.m_data = std::move(data);

    m_vertexElementsCount += stream.m_elementsCount;

    m_streams.push_back(std::move(stream));
}

MeshData::MeshData(const std::vector<VertexStream>& streams,
                   std::vector<uint16_t>&& indices, size_t verticesCount,
                   size_t vertexSizeBytes, size_t vertexElementsCount)  :   m_verticesCount(verticesCount), m_vertexSizeBytes(vertexSizeBytes),
                                                                            m_vertexBufferSizeBytes(0), m_indexBufferSizeBytes(0),
                                                                            m_indices(std::move(indices))
{
    assert(streams.size());
    assert(m_vertexSizeBytes);
    assert(m_indices.size());
    assert(m_verticesCount);
    assert(vertexElementsCount);

    m_vertexBufferSizeBytes = m_verticesCount * m_vertexSizeBytes;
    m_indexBufferSizeBytes = m_indices.size() * sizeof(uint16_t);
    m_vertices.resize(m_verticesCount * vertexElementsCount);

    // Interleave
    for (auto& stream : streams)
    {
        assert(stream.m_data.size() == (stream.m_elementsCount * m_verticesCount));

        for (size_t i = 0; i < verticesCount; ++i)
        {
            memcpy(&m_vertices[i * vertexElementsCount + stream.m_elementOffset],
                   &stream.m_data[i * stream.m_elementsCount],
                   stream.m_elementsCount * sizeof(float));
        }
    }
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

// https://knarkowicz.wordpress.com/2013/05/25/simple-gpuview-custom-event-markers/
GpuViewMarker::GpuViewMarker(const std::wstring& name, const wchar_t* uuid) : m_name(name)
{
    UuidFromString((RPC_WSTR)uuid, &guid);
    EventRegister(&guid, nullptr, nullptr, &m_eventHandle);
}

GpuViewMarker::~GpuViewMarker()
{
    EventUnregister(m_eventHandle);
}

void GpuViewMarker::Mark()
{
    EventWriteString(m_eventHandle, 0, 0, m_name.c_str());
}

InputController::InputController(HWND hwnd) : m_roInit(RO_INIT_MULTITHREADED)
{
    // https://stackoverflow.com/a/36468365
    // "TL;DR: If you are making a Windows desktop app that requires Windows 10, 
    //  then link with RuntimeObject.lib and add this to your app initialization 
    //  (replacing CoInitialize or CoInitializeEx):"
    AssertIfFailed(m_roInit);

    m_mouse.SetWindow(hwnd);
}

void InputController::Update()
{
    m_gamepadState = m_gamepad.GetState(0);
    if (m_gamepadState.IsConnected())
        m_gamepadTracker.Update(m_gamepadState);

    m_keyboardState = std::move(m_keyboard.GetState());
    if (m_keyboard.IsConnected())
        m_keyboardTracker.Update(m_keyboardState);
    
    if (m_mouse.IsConnected())
    {
        m_mouseState = std::move(m_mouse.GetState());
        m_mouseTracker.Update(m_mouseState);
    }
}

void InputController::SetMouseRelativeMode(bool enable)
{
    m_mouse.SetMode(enable ? DirectX::Mouse::MODE_RELATIVE : DirectX::Mouse::MODE_ABSOLUTE);
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

std::string D3D12Basics::ConvertFromUTF16ToUTF8(const std::wstring& str)
{
    auto outStrLength = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, 0, 0, 0, 0);
    assert(outStrLength);
    std::string outStr(outStrLength, 0);
    auto result = WideCharToMultiByte(CP_UTF8, 0, str.c_str(), -1, &outStr[0], static_cast<int>(outStr.size()), 0, 0);
    result;
    assert(result == outStr.size());

    return outStr;
}

std::wstring D3D12Basics::ConvertFromUTF8ToUTF16(const std::string& str)
{
    auto outStrLength = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, 0, 0);
    assert(outStrLength);
    std::wstring outStr(outStrLength, 0);
    auto result = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &outStr[0], static_cast<int>(outStr.size())); 
    result;
    assert(result == outStr.size());

    return outStr;
}

size_t D3D12Basics::AlignToPowerof2(size_t value, size_t alignmentPower2)
{
    return (value + (alignmentPower2 - 1)) & ~(alignmentPower2 - 1);
}

bool D3D12Basics::IsPowerOf2(size_t value)
{
    return (value & (value - 1)) == 0;
}

bool D3D12Basics::IsAlignedToPowerof2(size_t value, size_t alignmentPower2)
{
    return (value & (alignmentPower2 - 1)) == 0;
}