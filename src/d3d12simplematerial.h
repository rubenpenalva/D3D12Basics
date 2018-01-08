#pragma once

// d3d12 fwd decl
#include "d3d12fwd.h"

namespace D3D12Render
{
    class D3D12SimpleMaterial
    {
    public:
        D3D12SimpleMaterial(ID3D12DevicePtr d3d12Device);
        
        ~D3D12SimpleMaterial();

        ID3D12PipelineStatePtr GetPSO() const { return m_defaultPSO; }

        ID3D12RootSignaturePtr GetRootSignature() const { return m_rootSignature; }

    private:
        void Load();

        ID3D12DevicePtr         m_device;

        ID3D12PipelineStatePtr  m_defaultPSO;
        ID3D12RootSignaturePtr  m_rootSignature;
                
        D3D12_FEATURE_DATA_ROOT_SIGNATURE GetFeatureRootSignatureHighestVerSupported();
    };
}