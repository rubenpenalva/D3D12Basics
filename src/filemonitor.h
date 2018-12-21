#pragma once

// windows includes
#include <windows.h>

// c++ includes
#include <string>
#include <thread>
#include <vector>

namespace D3D12Basics
{
    class FileMonitor
    {
    public:
        FileMonitor(const std::wstring& path);
        ~FileMonitor();

        // NOTE https://stackoverflow.com/questions/14189440/c-class-member-callback-simple-examples
        // NOTE this forces the callbacks to be public if no friends want to be used
        template<class T>
        void AddListener(const std::wstring& fileName, void(T::* const callback)(void),
                         T* const listener)
        {
            m_callbacks.emplace(std::filesystem::absolute(fileName), std::bind(callback, listener));
        }

    protected:
        std::thread m_monitorThread;

        std::unordered_multimap<std::wstring, std::function<void()>> m_callbacks;

        HANDLE m_quitEvent;

        std::wstring m_pathToMonitor;
        
        void MonitorThread();
    };
}