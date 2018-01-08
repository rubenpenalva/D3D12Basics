#pragma once

// windows includes
#include <windows.h>

namespace Utils
{
    void AssertIfFailed(HRESULT hr);
    
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