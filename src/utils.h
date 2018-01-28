#pragma once

// windows includes
#include <windows.h>

// thirdparty includes
#include "DirectXTK12/Inc/SimpleMath.h"

namespace D3D12Basics
{
    using Float2    = DirectX::SimpleMath::Vector2;
    using Float3    = DirectX::SimpleMath::Vector3;
    using Matrix44  = DirectX::SimpleMath::Matrix;

    // NOTE: https://www.gnu.org/software/libc/manual/html_node/Mathematical-Constants.html
    constexpr float M_PI    = 3.14159265358979323846f;
    constexpr float M_2PI   = 2.0f * 3.14159265358979323846f;
    constexpr float M_PI_2  = M_PI * 0.5f;
    constexpr float M_PI_4  = M_PI * 0.25f;
    constexpr float M_PI_8  = M_PI * 0.125f;

    void AssertIfFailed(HRESULT hr);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    Float3 SphericalToCartersian(float longitude, float latitude, float altitude = 1.0f);

    struct Resolution
    {
        unsigned int    m_width;
        unsigned int    m_height;
        float           m_aspectRatio;
    };

    class CustomWindow
    {
    public:
        static Resolution GetResolution();

        CustomWindow();
        ~CustomWindow();

        HWND GetHWND() const { return m_hwnd; }

    private:
        HWND m_hwnd;
    };
}