#include "d3d12utils.h"

// C includes
#include <sstream>

// project includes
#include "utils.h"

using namespace D3D12Basics;

D3D12Basics::ID3DBlobPtr D3D12Basics::D3D12CompileBlob(const char* src, const char* target,
                                                       const char* mainName,
                                                       unsigned int flags)
{
    ID3DBlobPtr blob;

    ID3DBlobPtr errors;
    auto result = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, mainName,
        target, flags, 0, &blob, &errors);
    if (FAILED(result))
    {
        assert(errors);

        std::wstringstream converter;
        converter << "\n" << static_cast<const char*>(errors->GetBufferPointer());
        OutputDebugString(converter.str().c_str());

        return nullptr;
    }

    return blob;
}

D3D12_RASTERIZER_DESC D3D12Basics::CreateDefaultRasterizerState()
{
    return  D3D12_RASTERIZER_DESC 
    {
            /*FillMode*/                D3D12_FILL_MODE_SOLID,
            /*CullMode*/                D3D12_CULL_MODE_BACK,
            /*FrontCounterClockwise*/   FALSE, // NOTE isnt this a bit weird?
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

D3D12_RASTERIZER_DESC D3D12Basics::CreateRasterizerState_NoDepthClip()
{
    return  D3D12_RASTERIZER_DESC
    {
        /*FillMode*/                D3D12_FILL_MODE_SOLID,
        /*CullMode*/                D3D12_CULL_MODE_BACK,
        /*FrontCounterClockwise*/   FALSE, // NOTE isnt this a bit weird?
        /*DepthBias*/               D3D12_DEFAULT_DEPTH_BIAS,
        /*DepthBiasClamp*/          D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
        /*SlopeScaledDepthBias*/    D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
        /*DepthClipEnable*/         FALSE,
        /*MultisampleEnable*/       FALSE,
        /*AntialiasedLineEnable*/   FALSE,
        /*ForcedSampleCount*/       0,
        /*ConservativeRaster*/      D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
    };
}

// TODO: is there a more elegant way of constructing a D3D12_BLEND_DESC object?
D3D12_BLEND_DESC D3D12Basics::CreateDefaultBlendState()
{
    D3D12_RENDER_TARGET_BLEND_DESC defaultRenderTargetBlendDesc;
    defaultRenderTargetBlendDesc.BlendEnable            = FALSE;
    defaultRenderTargetBlendDesc.LogicOpEnable          = FALSE;
    defaultRenderTargetBlendDesc.SrcBlend               = D3D12_BLEND_ONE;
    defaultRenderTargetBlendDesc.DestBlend              = D3D12_BLEND_ZERO;
    defaultRenderTargetBlendDesc.BlendOp                = D3D12_BLEND_OP_ADD;
    defaultRenderTargetBlendDesc.SrcBlendAlpha          = D3D12_BLEND_ONE;
    defaultRenderTargetBlendDesc.DestBlendAlpha         = D3D12_BLEND_ZERO;
    defaultRenderTargetBlendDesc.BlendOpAlpha           = D3D12_BLEND_OP_ADD;
    defaultRenderTargetBlendDesc.LogicOp                = D3D12_LOGIC_OP_NOOP;
    defaultRenderTargetBlendDesc.RenderTargetWriteMask  = D3D12_COLOR_WRITE_ENABLE_ALL;
        
    D3D12_BLEND_DESC blendDesc;
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = defaultRenderTargetBlendDesc;

    return blendDesc;
}

D3D12_BLEND_DESC D3D12Basics::CreateAlphaBlendState()
{
    D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc;
    rtBlendDesc.BlendEnable             = TRUE;
    rtBlendDesc.LogicOpEnable           = FALSE;
    rtBlendDesc.SrcBlend                = D3D12_BLEND_SRC_ALPHA;
    rtBlendDesc.DestBlend               = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.BlendOp                 = D3D12_BLEND_OP_ADD;
    rtBlendDesc.SrcBlendAlpha           = D3D12_BLEND_INV_SRC_ALPHA;
    rtBlendDesc.DestBlendAlpha          = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOpAlpha            = D3D12_BLEND_OP_ADD;
    rtBlendDesc.LogicOp                 = D3D12_LOGIC_OP_CLEAR;
    rtBlendDesc.RenderTargetWriteMask   = D3D12_COLOR_WRITE_ENABLE_ALL;

    //UINT8 RenderTargetWriteMask;
    D3D12_BLEND_DESC blendDesc;
    blendDesc.AlphaToCoverageEnable     = FALSE;
    blendDesc.IndependentBlendEnable    = FALSE;

    for (UINT i = 0; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i)
        blendDesc.RenderTarget[i] = rtBlendDesc;

    return blendDesc;
}

D3D12_DESCRIPTOR_RANGE1 D3D12Basics::CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, unsigned int descriptorsCount)
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

D3D12_ROOT_PARAMETER1 D3D12Basics::CreateConstantsRootParameter(UINT shaderRegister, UINT constantsCount, UINT registerSpace,
                                                                D3D12_SHADER_VISIBILITY shaderVisibility)
{
    D3D12_ROOT_PARAMETER1 rootParameter;
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.ShaderRegister = shaderRegister;
    rootParameter.Constants.Num32BitValues = constantsCount;
    rootParameter.Constants.RegisterSpace = registerSpace;
    rootParameter.ShaderVisibility = shaderVisibility;
    return rootParameter;
}

D3D12_ROOT_PARAMETER1 D3D12Basics::CreateCBVRootParameter(UINT shaderRegister, UINT registerSpace, D3D12_ROOT_DESCRIPTOR_FLAGS flags,
                                                          D3D12_SHADER_VISIBILITY shaderVisibility)
{
    D3D12_ROOT_PARAMETER1 rootParameter;
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.ShaderRegister = shaderRegister;
    rootParameter.Descriptor.RegisterSpace = registerSpace;
    rootParameter.Descriptor.Flags = flags;
    rootParameter.ShaderVisibility = shaderVisibility;
    return rootParameter;
}

D3D12_ROOT_PARAMETER1 D3D12Basics::CreateDescTableRootParameter(D3D12_DESCRIPTOR_RANGE1* ranges, unsigned int rangesCount,
                                                                D3D12_SHADER_VISIBILITY shaderVisibility)
{
    D3D12_ROOT_PARAMETER1 rootParameter;
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = rangesCount;
    rootParameter.DescriptorTable.pDescriptorRanges = ranges;
    rootParameter.ShaderVisibility = shaderVisibility;
    return rootParameter;
}

D3D12_STATIC_SAMPLER_DESC D3D12Basics::CreateStaticLinearSamplerDesc()
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

void D3D12Basics::OutputDebugBlobErrorMsg(ID3DBlobPtr errorMsg)
{
    OutputDebugStringA(static_cast<LPCSTR>(errorMsg->GetBufferPointer()));
}

D3D12_RESOURCE_DESC D3D12Basics::CreateTexture2DDesc(unsigned int width, unsigned int height,
                                                     DXGI_FORMAT format, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC resourceDesc;
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc = { 1, 0 };
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = flags;

    return resourceDesc;
}