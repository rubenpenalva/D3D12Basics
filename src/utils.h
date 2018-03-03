#pragma once

// c++ includes
#include <chrono>

// windows includes
#include <windows.h>
#include <Evntprov.h>

// thirdparty includes
#include "DirectXTK12/Inc/SimpleMath.h"

namespace D3D12Basics
{
    using Float2    = DirectX::SimpleMath::Vector2;
    using Float3    = DirectX::SimpleMath::Vector3;
    using Matrix44  = DirectX::SimpleMath::Matrix;

    // NOTE: https://www.gnu.org/software/libc/manual/html_node/Mathematical-Constants.html
    constexpr float M_PI        = 3.14159265358979323846f;
    constexpr float M_2PI       = 2.0f * 3.14159265358979323846f;
    constexpr float M_PI_2      = M_PI * 0.5f;
    constexpr float M_PI_4      = M_PI * 0.25f;
    constexpr float M_PI_8      = M_PI * 0.125f;
    constexpr float M_RCP_PI    = 1.0f / M_PI;
    constexpr float M_RCP_2PI   = 1.0f / M_2PI;

    void AssertIfFailed(HRESULT hr);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    Float3 SphericalToCartersian(float longitude, float latitude, float altitude = 1.0f);

    struct Vector2i
    {
        unsigned int m_x;
        unsigned int m_y;
    };

    struct Resolution
    {
        unsigned int    m_width;
        unsigned int    m_height;
        float           m_aspectRatio;
    };

    class CustomWindow
    {
    public:
        CustomWindow(const Resolution& resolution);
        ~CustomWindow();

        HWND GetHWND() const { return m_hwnd; }

        const Resolution& GetResolution() const { return m_currentResolution; }

        void ChangeResolution(const RECT& windowRect);

        void ChangeFullscreenMode() { m_fullscreenChanged = true; }

        bool HasResolutionChanged() const { return m_resolutionChanged; }
        
        bool HasFullscreenChanged() const { return m_fullscreenChanged; }

        void ResetWndProcEventsState();

    private:
        Resolution  m_currentResolution;

        HWND m_hwnd;
        
        bool m_fullscreenChanged;
        bool m_resolutionChanged;

        void CreateCustomWindow();
    };

    class Timer
    {
    public:
        Timer();

        void Mark();

        // Elapsed time between two marks
        float ElapsedTime() const { return m_elapsedTime; }

        // Total time from the timer construction to the last mark
        float TotalTime() const { return m_totalTime; }

    private:
        std::chrono::high_resolution_clock::time_point m_mark;

        float m_elapsedTime;
        float m_totalTime;
    };

    class GpuViewMarker
    {
    public:
        GpuViewMarker(const std::wstring& name, const wchar_t* uuid);
        ~GpuViewMarker();

        void Mark();

    private:
        REGHANDLE m_eventHandle;
        GUID guid;
        std::wstring m_name;
    };
}