#pragma once

// directx includes
#include <d3d12.h>
#include <d3dcompiler.h>

// project includes
#include "d3d12fwd.h"

namespace D3D12Basics
{
    ID3DBlobPtr D3D12CompileBlob(const char* src, const char* target, const char* mainName,
                                 unsigned int flags = 0);

    D3D12_RASTERIZER_DESC CreateDefaultRasterizerState();
    D3D12_RASTERIZER_DESC CreateRasterizerState_NoDepthClip();

    // TODO: is there a more elegant way of constructing a D3D12_BLEND_DESC object?
    D3D12_BLEND_DESC CreateDefaultBlendState();

    D3D12_BLEND_DESC CreateAlphaBlendState();

    D3D12_DESCRIPTOR_RANGE1 CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, unsigned int descriptorsCount);

    D3D12_ROOT_PARAMETER1 CreateConstantsRootParameter(UINT shaderRegister, UINT constantsCount, UINT registerSpace,
                                                       D3D12_SHADER_VISIBILITY shaderVisibility);

    D3D12_ROOT_PARAMETER1 CreateCBVRootParameter(UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
                                                 D3D12_SHADER_VISIBILITY shaderVisibility);

    D3D12_ROOT_PARAMETER1 CreateDescTableRootParameter(D3D12_DESCRIPTOR_RANGE1* ranges, unsigned int rangesCount,
                                                       D3D12_SHADER_VISIBILITY shaderVisibility);

    D3D12_STATIC_SAMPLER_DESC CreateStaticLinearSamplerDesc();

    void OutputDebugBlobErrorMsg(ID3DBlobPtr errorMsg);

    D3D12_RESOURCE_DESC CreateTexture2DDesc(unsigned int width, unsigned int height, DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags);
}