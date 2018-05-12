#pragma once

// d3d12 fwd decl
#include "d3d12fwd.h"
#include "d3d12basicsfwd.h"

// d3d12 includes
#include <d3d12.h>

namespace D3D12Render
{
    class D3D12Material
    {
    public:
        
        D3D12Material(D3D12Render::D3D12Gpu* gpu);
        
        ~D3D12Material();

        ID3D12PipelineStatePtr GetPSO() const { return m_pso; }

        ID3D12RootSignaturePtr GetRootSignature() const { return m_rootSignature; }

    private:
        ID3D12PipelineStatePtr m_pso;
        ID3D12RootSignaturePtr m_rootSignature;

        void CreatePSO(D3D12Render::D3D12Gpu* gpu);
    };
}