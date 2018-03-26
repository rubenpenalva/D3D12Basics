#pragma once

// c++ includes
#include <chrono>

// windows includes
#include <windows.h>
#include <evntprov.h>

// thirdparty includes
#include <d3d12.h>
#include "directxtk12/simplemath.h"

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

    struct VertexDesc
    {
        bool m_uv0;
        bool m_normal;
        bool m_tangent;
    };

    struct VertexStream
    {
        size_t m_elementsCount;
        size_t m_elementOffset;

        std::vector<float> m_data;
    };

    class VertexStreams
    {
    public:
        VertexStreams();

        void AddStream(size_t elementsCount, std::vector<float>&& data);

        const std::vector<VertexStream>& GetStreams() const { return m_streams; }

        size_t VertexElementsCount() const { return m_vertexElementsCount; }

    private:
        size_t m_vertexElementsCount;

        std::vector<VertexStream> m_streams;
    };

    class Mesh
    {
    public:
        static const uint32_t m_maxVertexCount = 0x0000ffff;

        Mesh() {}

        Mesh(const std::vector<VertexStream>& streams, std::vector<uint16_t>&& indices, 
             size_t verticesCount, size_t vertexSizeBytes, size_t vertexElementsCount);

        size_t VerticesCount() const { return m_verticesCount; }
        size_t VertexSizeBytes() const { return m_vertexSizeBytes; }

        size_t IndicesCount() const { return m_indices.size(); }

        const std::vector<float>& Vertices() const { return m_vertices; }
        const std::vector<uint16_t>& Indices() const { return m_indices; }

        size_t VertexBufferSizeBytes() const { return m_vertexBufferSizeBytes; }
        size_t IndexBufferSizeBytes() const { return m_indexBufferSizeBytes; }

    private:
        size_t m_verticesCount;
        size_t m_vertexSizeBytes;
        size_t m_vertexBufferSizeBytes;
        size_t m_indexBufferSizeBytes;

        std::vector<float>      m_vertices;
        std::vector<uint16_t>   m_indices;
    };

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

    void AssertIfFailed(HRESULT hr);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    Float3 SphericalToCartersian(float longitude, float latitude, float altitude = 1.0f);

    std::string ConvertFromUTF16ToUTF8(const std::wstring& str);

    std::wstring ConvertFromUTF8ToUTF16(const std::string& str);
}