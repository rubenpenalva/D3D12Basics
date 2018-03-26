#pragma once

// d3d12 fwd decl
#include "d3d12fwd.h"
#include "d3d12basicsfwd.h"

// d3d12 includes
#include <d3d12.h>

namespace D3D12Render
{
    using D3D12MaterialResources = std::vector<D3D12ResourceID>;

    class D3D12Material
    {
    public:
        
        D3D12Material(ID3D12DevicePtr d3d12Device);
        
        ~D3D12Material();

        ID3D12PipelineStatePtr GetPSO() const { return m_defaultPSO; }

        ID3D12RootSignaturePtr GetRootSignature() const { return m_rootSignature; }

        void Apply(ID3D12GraphicsCommandListPtr commandList, D3D12_GPU_DESCRIPTOR_HANDLE cbv,
                    D3D12_GPU_DESCRIPTOR_HANDLE srv);

    private:
        void Load();

        ID3D12DevicePtr                 m_device;

        ID3D12PipelineStatePtr          m_defaultPSO;
        ID3D12RootSignaturePtr          m_rootSignature;

        D3D12_FEATURE_DATA_ROOT_SIGNATURE GetFeatureRootSignatureHighestVerSupported();
    };
}