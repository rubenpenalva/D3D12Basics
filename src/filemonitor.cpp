#include "filemonitor.h"

// windows includes
#include <windows.h>

// c++ includes
#include <cassert>
#include <filesystem>

// project includes
#include "utils.h"

using namespace D3D12Basics;

FileMonitor::FileMonitor(const std::wstring& path) : m_pathToMonitor(std::filesystem::absolute(path))
{
    m_quitEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(m_quitEvent);

    m_monitorThread = std::thread(&FileMonitor::MonitorThread, this);
}

FileMonitor::~FileMonitor()
{
    AssertIfFailed(SetEvent(m_quitEvent));
    m_monitorThread.join();

    CloseHandle(m_quitEvent);
}

void FileMonitor::MonitorThread()
{
    AssertIfFailed(SetThreadDescription(GetCurrentThread(), L"FileMonitor thread"));

    HANDLE changeHandle = FindFirstChangeNotificationW(m_pathToMonitor.c_str(), TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE);
    std::vector<HANDLE> handles{ changeHandle, m_quitEvent };

    uint8_t notifyInformationBuffer[1024];
    DWORD bytesReturned;
    while (1)
    {
        auto result = WaitForMultipleObjects(static_cast<DWORD>(handles.size()), &handles[0], FALSE, INFINITE);
        AssertIfFailed(result, WAIT_FAILED);

        if (result == (WAIT_OBJECT_0 + 1))
            break;

        AssertIfFailed(ReadDirectoryChangesW(changeHandle, &notifyInformationBuffer[0],
                                             sizeof(uint8_t) * 1024,
                                             TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE,
                                             &bytesReturned, 0, 0));
        assert(bytesReturned);

        FILE_NOTIFY_INFORMATION* notifyInformation = nullptr;
        for (DWORD currentSize = 0; currentSize < bytesReturned; currentSize += notifyInformation->NextEntryOffset)
        {
            notifyInformation = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(&notifyInformationBuffer[0] + currentSize);
            assert(notifyInformation);

            // TODO if modifying a shader file from visual studio, the filename is the name of the folder (shaders)
            // instead of the name of the shader file. Dont want to waste time on fixing this. Modifying the shaders
            // with sublime works just fine.
            const std::wstring fileName = std::wstring(notifyInformation->FileName, static_cast<int>(notifyInformation->FileNameLength / sizeof(WCHAR)));

            std::wstring absolutePath = std::filesystem::absolute(m_pathToMonitor + L"/" + fileName);
            auto range = m_callbacks.equal_range(absolutePath);
            if (range.first == range.second)
                OutputDebugString((L"FileMonitor::MonitorThread file not being listened to " + absolutePath + L"\n").c_str());
            for (auto it = range.first; it != range.second; ++it)
                it->second();

            if (notifyInformation->NextEntryOffset == 0)
                break;
        }

        AssertIfFailed(FindNextChangeNotification(changeHandle));
    }

    AssertIfFailed(FindCloseChangeNotification(changeHandle));
}