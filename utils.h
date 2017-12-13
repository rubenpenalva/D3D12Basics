#pragma once

// windows includes
#include <windows.h>
//#include <wrl.h>

namespace Utils
{
    void AssertIfFailed(HRESULT hr);
    
    struct Resolution
    {
        unsigned int m_width;
        unsigned int m_height;
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