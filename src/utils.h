#pragma once

// c++ includes
#include <chrono>
#include <vector>
#include <array>

// windows includes
#include <windows.h>
#include <evntprov.h>
#include <wrl/event.h>

// thirdparty includes
#include <d3d12.h>
#include "directxtk12/simplemath.h"

namespace D3D12Basics
{
    const uint32_t g_1kb    = 1 << 10;
    const uint32_t g_2kb    = g_1kb << 1;
    const uint32_t g_4kb    = g_2kb << 1;
    const uint32_t g_8kb    = g_4kb << 1;
    const uint32_t g_16kb   = g_8kb << 1;
    const uint32_t g_32kb   = g_16kb << 1;
    const uint32_t g_64kb   = g_32kb << 1;
    const uint32_t g_128kb  = g_64kb << 1;
    const uint32_t g_256kb  = g_128kb << 1;
    const uint32_t g_512kb  = g_256kb << 1;
    const uint32_t g_1mb    = g_512kb << 1;
    const uint32_t g_2mb    = g_1mb << 1;
    const uint32_t g_4mb    = g_2mb << 1;

    using Float2    = DirectX::SimpleMath::Vector2;
    using Float3    = DirectX::SimpleMath::Vector3;
    using Float4    = DirectX::SimpleMath::Vector4;
    using Matrix44  = DirectX::SimpleMath::Matrix;

    using hr_clock  = std::chrono::high_resolution_clock;

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
        bool m_tangent_bitangent;
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

    class MeshData
    {
    public:
        static const uint32_t m_maxVertexCount = 0x0000ffff;

        MeshData() {}

        MeshData(const std::vector<VertexStream>& streams, std::vector<uint16_t>&& indices,
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
    
    constexpr bool IsPowerOf2(size_t value)
    {
        return (value & (value - 1)) == 0;
    }

    // TODO make it a bit more stdish?
    template<class T, uint8_t Size = 128>
    class CircularBuffer
    {
        static_assert(Size != 0);
        static_assert(D3D12Basics::IsPowerOf2(Size), "CircularBuffer Size has to be power of 2 because of Next function");

    public:
        using Array = std::array<float, Size>;

        static size_t CalculateCircularIndex(size_t index)
        {
            return index & (Size - 1);
        }

        CircularBuffer() : m_values{}, m_nextIndex{}, m_lastIndex{}
        {
        }

        size_t StartIndex() const { return m_nextIndex; }

        void Next() 
        { 
            m_lastIndex = m_nextIndex;
            m_nextIndex = CalculateCircularIndex(m_nextIndex + 1);
        }
        
        void SetValue(const T& value) { m_values[m_nextIndex] = value; }

        const T& LastValue() const { return m_values[m_lastIndex]; }

        const Array& Values() const { return m_values; }

    private:
        std::array<T, Size> m_values;
        size_t m_nextIndex;
        size_t m_lastIndex;
    };

    // TODO dont like the interface of stopclock and runningtime.
    // its not obvious how to use it. maybe with the start/stop pair
    // should be easier
    class StopClock
    {
    public:
        using SplitTimeBuffer       = CircularBuffer<float, 32>;
        using SplitTimeBufferPtr    = std::unique_ptr<SplitTimeBuffer>;

        StopClock();

        void Mark();

        void ResetMark();

        const SplitTimeBuffer& SplitTimes() const { return m_splitTimes; }

        float AverageSplitTime() const;

    private:
        hr_clock::time_point m_last;

        SplitTimeBuffer m_splitTimes;

        void AddSplitTime(const hr_clock::time_point& begin, const hr_clock::time_point& end);
    };

    class RunningTime
    {
    public:
        RunningTime();

        void Reset();

        float Time() const;

    private:
        hr_clock::time_point m_startTime;
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
    void AssertIfFailed(BOOL b);
    void AssertIfFailed(DWORD d, DWORD failValue);

    // parallels = latitude = altitude = phi 
    // meridians = longitude = azimuth = theta
    Float3 SphericalToCartersian(float longitude /* theta */, float latitude /* phi */, float altitude = 1.0f);
    Float3 DDLonSphericalToCartesian(float longitude /* theta */, float latitude /* phi */, float altitude = 1.0f);
    Float3 DDLatSphericalToCartesian(float longitude /* theta */, float latitude /* phi */, float altitude = 1.0f);

    std::string ConvertFromUTF16ToUTF8(const std::wstring& str);

    std::wstring ConvertFromUTF8ToUTF16(const std::string& str);

    // alignmentPower2 has to be a power of 2
    size_t AlignToPowerof2(size_t value, size_t alignmentPower2);

    bool IsAlignedToPowerof2(size_t value, size_t alignmentPower2);

    std::vector<char> ReadFullFile(const std::wstring& fileName, bool readAsBinary = false);
}