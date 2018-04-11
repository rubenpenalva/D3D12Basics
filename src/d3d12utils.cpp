#include "d3d12utils.h"

// C includes
#include <sstream>

// project includes
#include "utils.h"

using namespace D3D12Basics;

D3D12_RASTERIZER_DESC D3D12Render::CreateDefaultRasterizerState()
{
    return  D3D12_RASTERIZER_DESC 
    {
            /*FillMode*/                D3D12_FILL_MODE_SOLID,
            /*CullMode*/                D3D12_CULL_MODE_BACK,
            /*FrontCounterClockwise*/   FALSE,
            /*DepthBias*/               D3D12_DEFAULT_DEPTH_BIAS,
            /*DepthBiasClamp*/          D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
            /*SlopeScaledDepthBias*/    D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
            /*DepthClipEnable*/         TRUE,
            /*MultisampleEnable*/       FALSE,
            /*AntialiasedLineEnable*/   FALSE,
            /*ForcedSampleCount*/       0,
            /*ConservativeRaster*/      D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF 
    };
}

// TODO: is there a more elegant way of constructing a D3D12_BLEND_DESC object?
D3D12_BLEND_DESC D3D12Render::CreateDefaultBlendState()
{
    const D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc =
    {
        FALSE,FALSE,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_OP_ADD,
        D3D12_LOGIC_OP_NOOP,
        D3D12_COLOR_WRITE_ENABLE_ALL,
    };
        
    D3D12_BLEND_DESC blendDesc
    {
        /*AlphaToCoverageEnable*/   FALSE,
        /*IndependentBlendEnable*/  FALSE,
        /*RenderTarget*/ {}
    };

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

    return blendDesc;
}

D3D12_BLEND_DESC D3D12Render::CreateAlphaBlendState()
{
    D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc;
    rtBlendDesc.BlendEnable     = TRUE;
    rtBlendDesc.LogicOpEnable   = FALSE;
    rtBlendDesc.SrcBlend        = D3D12_BLEND_SRC_ALPHA;
    rtBlendDesc.DestBlend       = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.BlendOp         = D3D12_BLEND_OP_ADD;
    rtBlendDesc.SrcBlendAlpha   = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.DestBlendAlpha  = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOpAlpha    = D3D12_BLEND_OP_ADD;
    rtBlendDesc.LogicOp         = D3D12_LOGIC_OP_CLEAR;

    //UINT8 RenderTargetWriteMask;
    D3D12_BLEND_DESC blendDesc;
    blendDesc.AlphaToCoverageEnable     = FALSE;
    blendDesc.IndependentBlendEnable    = FALSE;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = rtBlendDesc;

    return blendDesc;
}

D3D12Render::ID3DBlobPtr D3D12Render::CompileShader(const char* src, const char* mainName, const char* shaderModel, unsigned int compileFlags)
{
    ID3DBlobPtr shader;
#if _DEBUG
    ID3DBlobPtr errors;
    AssertIfFailed(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, mainName, shaderModel, compileFlags, 0, &shader, &errors));
    if (errors)
    {
        std::wstringstream converter;
        converter << static_cast<const char*>(errors->GetBufferPointer());
        OutputDebugString(converter.str().c_str());
    }
#else
    D3D12Basics::AssertIfFailed(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, mainName, shaderModel, compileFlags, 0, &shader, nullptr));
#endif
    return shader;
}

D3D12_DESCRIPTOR_RANGE1 D3D12Render::CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, unsigned int descriptorsCount)
{
    D3D12_DESCRIPTOR_RANGE1 range;
    range.RangeType = rangeType;
    range.NumDescriptors = descriptorsCount;
    range.BaseShaderRegister = 0;
    range.RegisterSpace = 0;
    range.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    return range;
}

D3D12_ROOT_PARAMETER1 D3D12Render::CreateDescTableRootParameter(D3D12_DESCRIPTOR_RANGE1* ranges, unsigned int rangesCount,
                                                    D3D12_SHADER_VISIBILITY shaderVisibility)
{
    D3D12_ROOT_PARAMETER1 rootParameter;
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = rangesCount;
    rootParameter.DescriptorTable.pDescriptorRanges = ranges;
    rootParameter.ShaderVisibility = shaderVisibility;
    return rootParameter;
}

D3D12_STATIC_SAMPLER_DESC D3D12Render::CreateStaticLinearSamplerDesc()
{
    D3D12_STATIC_SAMPLER_DESC staticSamplerDesc
    {
        /*D3D12_FILTER Filter*/ D3D12_FILTER_ANISOTROPIC,
        /*D3D12_TEXTURE_ADDRESS_MODE AddressU*/ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        /*D3D12_TEXTURE_ADDRESS_MODE AddressV*/ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        /*D3D12_TEXTURE_ADDRESS_MODE AddressW*/ D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        /*FLOAT MipLODBias*/ 0.0f,
        /*UINT MaxAnisotropy*/ 1,
        /*D3D12_COMPARISON_FUNC ComparisonFunc*/ D3D12_COMPARISON_FUNC_LESS_EQUAL,
        /*D3D12_STATIC_BORDER_COLOR BorderColor*/ D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
        /*FLOAT MinLOD*/ 0.0f,
        /*FLOAT MaxLOD*/ D3D12_FLOAT32_MAX,
        /*UINT ShaderRegister*/ 0,
        /*UINT RegisterSpace*/ 0,
        /*D3D12_SHADER_VISIBILITY ShaderVisibility*/ D3D12_SHADER_VISIBILITY_PIXEL
    };

    return staticSamplerDesc;
}