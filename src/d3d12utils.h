#pragma once

// directx includes
#include <d3d12.h>
#include <d3dcompiler.h>

// project includes
#include "d3d12fwd.h"

namespace D3D12Render
{
    D3D12_RASTERIZER_DESC CreateDefaultRasterizerState();

    // TODO: is there a more elegant way of constructing a D3D12_BLEND_DESC object?
    D3D12_BLEND_DESC CreateDefaultBlendState();

    D3D12_BLEND_DESC CreateAlphaBlendState();

    ID3DBlobPtr CompileShader(const char* src, const char* mainName, const char* shaderModel, unsigned int compileFlags);

    D3D12_DESCRIPTOR_RANGE1 CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, unsigned int descriptorsCount);

    D3D12_ROOT_PARAMETER1 CreateDescTableRootParameter(D3D12_DESCRIPTOR_RANGE1* ranges, unsigned int rangesCount,
                                                       D3D12_SHADER_VISIBILITY shaderVisibility);

    D3D12_STATIC_SAMPLER_DESC CreateStaticLinearSamplerDesc();
}