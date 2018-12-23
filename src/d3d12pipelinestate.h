#pragma once

// project includes
#include "filemonitor.h"
#include "d3d12gpu.h"

// c++ includes
#include <mutex>

namespace D3D12Basics
{
    struct D3D12PipelineStateDesc
    {
        using InputElements = std::vector<D3D12_INPUT_ELEMENT_DESC>;

        InputElements                   m_inputElements;
        std::wstring                    m_rootSignatureFullPath;
        std::wstring                    m_gpuProgramFullPath;
        D3D12_RASTERIZER_DESC           m_rasterizerDesc;
        D3D12_BLEND_DESC                m_blendDesc;
        D3D12_DEPTH_STENCIL_DESC        m_depthStencilDesc;
        D3D12_PRIMITIVE_TOPOLOGY_TYPE   m_topologyType;
        D3D12_PRIMITIVE_TOPOLOGY        m_topology;
        std::vector<DXGI_FORMAT>        m_rtsFormat;
        DXGI_FORMAT                     m_dsvFormat;
        DXGI_SAMPLE_DESC                m_sampleDesc;
    };

    class D3D12PipelineState
    {
    public:
        D3D12PipelineState(D3D12Gpu& gpu, FileMonitor& fileMonitor, const D3D12PipelineStateDesc& pipeDesc, 
                            const std::wstring& debugName);

        bool ApplyState(ID3D12GraphicsCommandListPtr cmdList);

    private:
        struct State
        {
            uint64_t m_frameId;

            ID3D12RootSignaturePtr m_rs;
            std::vector<ID3DBlobPtr> m_shaders;
            ID3D12PipelineStatePtr m_pso;
            D3D12_GRAPHICS_PIPELINE_STATE_DESC m_desc;
        };

        D3D12Gpu& m_gpu;

        std::wstring m_rootSignatureFullPath;
        std::wstring m_programFullPath;

        std::wstring m_debugName;

        int m_lastActivatedState;
        State m_pipeStates[2];

        ID3D12RootSignaturePtr m_updatedRS;
        std::vector<ID3DBlobPtr> m_updatedShaders;

        bool m_isUpdatePending;

        std::mutex m_mutex;

        D3D12_PRIMITIVE_TOPOLOGY m_topology;

        void UpdateRootSignature();
        void UpdateShaders();

        ID3D12RootSignaturePtr BuildRS(const std::vector<char>& src);
        ID3D12RootSignaturePtr BuildRSFromFile();

        std::vector<ID3DBlobPtr> BuildShaders(const std::vector<char>& src);
        std::vector<ID3DBlobPtr> BuildShadersFromFile();

        bool UpdateState(State& state);

        bool ConstructStates();

        bool IsStateValid(const State& state);
    };
}