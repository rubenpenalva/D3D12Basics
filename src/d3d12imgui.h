#pragma once

#include "d3d12gpu.h"
#include "d3d12pipelinestate.h"

struct ImDrawData;

namespace D3D12Basics
{
    class D3D12ImGui
    {
    public:
        D3D12ImGui(D3D12Basics::D3D12Gpu& gpu, FileMonitor& fileMonitor);
        ~D3D12ImGui();

        void BeginFrame();

        void EndFrame(ID3D12GraphicsCommandListPtr cmdList);

    private:
        D3D12Basics::D3D12Gpu& m_gpu;

        D3D12PipelineState m_pipelineState;

        D3D12GpuViewHandle m_textureView;

        D3D12_VIEWPORT m_defaultViewport;

        size_t                  m_vertexBufferSizeBytes;
        D3D12GpuMemoryHandle    m_vertexBuffer;

        size_t                  m_indexBufferSizeBytes;
        D3D12GpuMemoryHandle    m_indexBuffer;

        D3D12GpuMemoryHandle    m_transformation;

        void CreateFontTexture();

        void CreateVertexBuffer(ImDrawData* drawData);

        void CreateIndexBuffer(ImDrawData* drawData);

        void UpdateBuffers(ImDrawData* drawData);
    };
}