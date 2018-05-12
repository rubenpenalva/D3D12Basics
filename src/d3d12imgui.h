#pragma once

#include "d3d12gpu.h"

struct ImDrawData;

namespace D3D12Render
{
    class D3D12ImGui
    {
    public:
        D3D12ImGui(D3D12Render::D3D12Gpu* gpu);
        ~D3D12ImGui();

        void BeginFrame();

        std::vector<D3D12GpuRenderTask> EndFrame();

    private:
        D3D12Render::D3D12Gpu* m_gpu;

        ID3D12PipelineStatePtr m_pso;
        ID3D12RootSignaturePtr m_rootSignature;

        D3D12GpuMemoryView m_textureView;

        D3D12_VIEWPORT m_defaultViewport;

        size_t                  m_vertexBufferSizeBytes;
        D3D12GpuMemoryHandle    m_vertexBuffer;

        size_t                  m_indexBufferSizeBytes;
        D3D12GpuMemoryHandle    m_indexBuffer;

        D3D12GpuMemoryHandle    m_transformation;

        void CreatePSO();

        void CreateFontTexture();

        void CreateVertexBuffer(ImDrawData* drawData);

        void CreateIndexBuffer(ImDrawData* drawData);

        void UpdateBuffers(ImDrawData* drawData);
    };
}