#include "d3d12pipelinestate.h"

// c++ includes
#include <cassert>
#include <filesystem>
#include <algorithm>

// project includes
#include "d3d12utils.h"

namespace
{
    const char* g_vertexShaderMainName = "VertexShaderMain";
    const char* g_vertexShaderMainNameEnd = g_vertexShaderMainName + 16;
    const char* g_vertexShaderTarget = "vs_5_1";
    const char* g_pixelShaderMainName = "PixelShaderMain";
    const char* g_pixelShaderMainNameEnd = g_pixelShaderMainName + 15;
    const char* g_pixelShaderTarget = "ps_5_1";
    const char* g_rootSignatureTarget = "rootsig_1_1";
    const char* g_rootSignatureName = "MyRS1";

    enum ShaderIndex
    {
        VS,
        PS
    };
}

using namespace D3D12Basics;

D3D12PipelineState::D3D12PipelineState(D3D12Gpu& gpu, FileMonitor& fileMonitor,
                                        const D3D12PipelineStateDesc& pipeDesc,
                                        const std::wstring& debugName) : m_gpu(gpu),
                                                                         m_debugName(debugName),
                                                                         m_rootSignatureFullPath(pipeDesc.m_rootSignatureFullPath),
                                                                         m_programFullPath(pipeDesc.m_gpuProgramFullPath),
                                                                         m_isUpdatePending(false),
                                                                         m_lastActivatedState(0),
                                                                         m_topology(pipeDesc.m_topology)
{
    // root signature file
    {
        assert(std::filesystem::exists(m_rootSignatureFullPath));
        fileMonitor.AddListener(m_rootSignatureFullPath, &D3D12PipelineState::UpdateRootSignature, this);
    }
    
    // vertex and pixel shader file
    {
        assert(std::filesystem::exists(m_programFullPath));
        fileMonitor.AddListener(m_programFullPath, &D3D12PipelineState::UpdateShaders, this);
    }

    // Init pipeline states
    auto& pipeState = m_pipeStates[0];
    {
        pipeState = {};
        pipeState.m_desc.InputLayout = { &pipeDesc.m_inputElements[0], 
                                          static_cast<UINT>(pipeDesc.m_inputElements.size()) };
        pipeState.m_desc.RasterizerState = pipeDesc.m_rasterizerDesc;
        pipeState.m_desc.BlendState = pipeDesc.m_blendDesc;
        pipeState.m_desc.DepthStencilState = pipeDesc.m_depthStencilDesc;
        pipeState.m_desc.SampleMask = UINT_MAX;
        pipeState.m_desc.PrimitiveTopologyType = pipeDesc.m_topologyType;
        pipeState.m_desc.NumRenderTargets = static_cast<UINT>(pipeDesc.m_rtsFormat.size());
        assert(pipeState.m_desc.NumRenderTargets < 8);
        if (pipeDesc.m_rtsFormat.size())
            memcpy(&pipeState.m_desc.RTVFormats[0], &pipeDesc.m_rtsFormat[0], sizeof(DXGI_FORMAT) * pipeDesc.m_rtsFormat.size());
        pipeState.m_desc.DSVFormat = pipeDesc.m_dsvFormat;
        pipeState.m_desc.SampleDesc = pipeDesc.m_sampleDesc;
    }

    if (!ConstructStates())
        OutputDebugString((L"Pipeline state construction failed " + m_debugName).c_str());
}

bool D3D12PipelineState::ApplyState(ID3D12GraphicsCommandListPtr cmdList)
{
    // If the update state is freshly updated, try to find
    // a staging state from m_pipeStates that is not currently
    // in flight and swap it
    {
        std::lock_guard lock(m_mutex);

        if (m_isUpdatePending)
        {
            // Find a state that has already being retired from the gpu
            for (int i = 0; i < 2; ++i)
            {
                if (m_gpu.IsFrameFinished(m_pipeStates[i].m_frameId))
                {
                    m_lastActivatedState = i;
                    UpdateState(m_pipeStates[i]);
                    m_isUpdatePending = false;
                    break;
                }
            }
        }
    }

    auto& activeState = m_pipeStates[m_lastActivatedState];

    if (!IsStateValid(activeState))
    {
        // TODO spaming log. figure out a better way.
        //OutputDebugString((L"D3D12PipelineState::ApplyState invalid active state" + m_debugName + L"\n").c_str());
        return false;
    }
    activeState.m_frameId = m_gpu.GetCurrentFrameId();

    cmdList->IASetPrimitiveTopology(m_topology);
    cmdList->SetPipelineState(activeState.m_pso.Get());
    cmdList->SetGraphicsRootSignature(activeState.m_rs.Get());

    return true;
}

void D3D12PipelineState::UpdateRootSignature()
{
    auto rs = BuildRSFromFile();
    if (!rs)
        return;

    std::lock_guard lock(m_mutex);

    m_updatedRS = rs;

    m_isUpdatePending = true;
}

void D3D12PipelineState::UpdateShaders()
{
    auto shaders = BuildShadersFromFile();

    std::lock_guard lock(m_mutex);

    m_updatedShaders = std::move(shaders);

    m_isUpdatePending = true;
}

ID3D12RootSignaturePtr D3D12PipelineState::BuildRS(const std::vector<char>& src)
{
    auto rsBlob = D3D12CompileBlob(&src[0], g_rootSignatureTarget, g_rootSignatureName);
    if (!rsBlob)
        return nullptr;

    return m_gpu.CreateRootSignature(rsBlob, m_debugName.c_str());
}

ID3D12RootSignaturePtr D3D12PipelineState::BuildRSFromFile()
{
    std::vector<char> rootSignatureSrc = ReadFullFile(m_rootSignatureFullPath);

    return BuildRS(rootSignatureSrc);
}

std::vector<ID3DBlobPtr> D3D12PipelineState::BuildShaders(const std::vector<char>& src)
{
    const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;

    if (std::search(src.begin(), src.end(), g_vertexShaderMainName, g_vertexShaderMainNameEnd) == src.cend())
        return {};

    auto vertexShader = D3D12CompileBlob(&src[0], g_vertexShaderTarget, g_vertexShaderMainName, compileFlags);
    if (!vertexShader)
        return {};

    bool isPSRequested = std::search(src.cbegin(), src.cend(), g_pixelShaderMainName, g_pixelShaderMainNameEnd) != src.cend();

    auto pixelShader = isPSRequested ? D3D12CompileBlob(&src[0], g_pixelShaderTarget, g_pixelShaderMainName, compileFlags) : nullptr;
    if (!pixelShader && isPSRequested)
        return {};

    std::vector<ID3DBlobPtr> shaders;
    shaders.resize(2);
    shaders[ShaderIndex::VS] = vertexShader;
    shaders[ShaderIndex::PS] = isPSRequested ? pixelShader : nullptr;

    return shaders;
}

std::vector<ID3DBlobPtr> D3D12PipelineState::BuildShadersFromFile()
{
    std::vector<char> src = ReadFullFile(m_programFullPath);
    return BuildShaders(src);
}

bool D3D12PipelineState::UpdateState(State& state)
{
    if (!m_updatedRS && m_updatedShaders.empty())
        return false;

    auto stateCopy = state;
    if (m_updatedRS)
    {
        stateCopy.m_rs = m_updatedRS;
        stateCopy.m_desc.pRootSignature = m_updatedRS.Get();
        m_updatedRS = nullptr;
    }
    if (!m_updatedShaders.empty())
    {
        stateCopy.m_shaders = std::move(m_updatedShaders);
        stateCopy.m_desc.VS = { stateCopy.m_shaders[ShaderIndex::VS]->GetBufferPointer(),
                                stateCopy.m_shaders[ShaderIndex::VS]->GetBufferSize() };
        stateCopy.m_desc.PS = {};
        if (stateCopy.m_shaders[ShaderIndex::PS])
            stateCopy.m_desc.PS = { stateCopy.m_shaders[ShaderIndex::PS]->GetBufferPointer(),
                                    stateCopy.m_shaders[ShaderIndex::PS]->GetBufferSize() };
    }

    stateCopy.m_pso = m_gpu.CreatePSO(stateCopy.m_desc, m_debugName.c_str());
    if (!stateCopy.m_pso)
        return false;

    state = std::move(stateCopy);

    return true;
}

bool D3D12PipelineState::ConstructStates()
{
    UpdateRootSignature();
    UpdateShaders();

    m_lastActivatedState = 0;
    if (!UpdateState(m_pipeStates[m_lastActivatedState]))
        return false;

    assert(IsStateValid(m_pipeStates[m_lastActivatedState]));

    m_pipeStates[m_lastActivatedState + 1] = m_pipeStates[m_lastActivatedState];

    return true;
}

bool D3D12PipelineState::IsStateValid(const State& state)
{
    return state.m_rs && state.m_pso;
}