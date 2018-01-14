#include "d3d12simplematerial.h"

// C includes
#include <cassert>

// C++ includes
#include <sstream>

// directx includes
#include <d3d12.h>
#include <d3dcompiler.h>

// project includes
#include "utils.h"

using namespace D3D12Render;
using namespace Utils;

namespace
{
    D3D12_RASTERIZER_DESC CreateDefaultRasterizerState()
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
    D3D12_BLEND_DESC CreateDefaultBlendState()
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

    ID3DBlobPtr CompileShader(const char* src, const char* mainName, const char* shaderModel, unsigned int compileFlags)
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
        AssertIfFailed(D3DCompile(src, strlen(src), nullptr, nullptr, nullptr, mainName, shaderModel, compileFlags, 0, &shader, nullptr));
#endif
        return shader;
    }

    D3D12_DESCRIPTOR_RANGE1 CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE rangeType, unsigned int descriptorsCount)
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

    D3D12_ROOT_PARAMETER1 CreateDescTableRootParameter(D3D12_DESCRIPTOR_RANGE1* ranges, unsigned int rangesCount, 
                                                        D3D12_SHADER_VISIBILITY shaderVisibility)
    {
        D3D12_ROOT_PARAMETER1 rootParameter;
        rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameter.DescriptorTable.NumDescriptorRanges = rangesCount;
        rootParameter.DescriptorTable.pDescriptorRanges = ranges;
        rootParameter.ShaderVisibility = shaderVisibility;
        return rootParameter;
    }

    D3D12_STATIC_SAMPLER_DESC CreateStaticSamplerDesc(D3D12_FILTER filter, D3D12_TEXTURE_ADDRESS_MODE adddresMode)
    {
        D3D12_STATIC_SAMPLER_DESC staticSamplerDesc
        {
            /*D3D12_FILTER Filter*/ filter,
            /*D3D12_TEXTURE_ADDRESS_MODE AddressU*/ adddresMode,
            /*D3D12_TEXTURE_ADDRESS_MODE AddressV*/ adddresMode,
            /*D3D12_TEXTURE_ADDRESS_MODE AddressW*/ adddresMode,
            /*FLOAT MipLODBias*/ 0.0f,
            /*UINT MaxAnisotropy*/ 1,
            /*D3D12_COMPARISON_FUNC ComparisonFunc*/ D3D12_COMPARISON_FUNC_ALWAYS,
            /*D3D12_STATIC_BORDER_COLOR BorderColor*/ D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
            /*FLOAT MinLOD*/ 0.0f,
            /*FLOAT MaxLOD*/ D3D12_FLOAT32_MAX,
            /*UINT ShaderRegister*/ 0,
            /*UINT RegisterSpace*/ 0,
            /*D3D12_SHADER_VISIBILITY ShaderVisibility*/ D3D12_SHADER_VISIBILITY_PIXEL
        };

        return staticSamplerDesc;
    }
}

D3D12SimpleMaterial::D3D12SimpleMaterial(ID3D12DevicePtr d3d12Device) : m_device(d3d12Device)
{
    assert(m_device);

    Load();
}

D3D12SimpleMaterial::~D3D12SimpleMaterial()
{}

void D3D12SimpleMaterial::Load()
{
    D3D12_DESCRIPTOR_RANGE1 vertexCBVRanges = CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1);
    D3D12_DESCRIPTOR_RANGE1 pixelSRVRanges = CreateDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1);
    
    const unsigned rootParametersCount = 2;
    D3D12_ROOT_PARAMETER1 rootParameters[rootParametersCount]
    { 
        CreateDescTableRootParameter(&vertexCBVRanges, 1, D3D12_SHADER_VISIBILITY_VERTEX),
        CreateDescTableRootParameter(&pixelSRVRanges, 1, D3D12_SHADER_VISIBILITY_PIXEL) 
    };
    
    const unsigned int staticSamplerDescsCount = 1;
    D3D12_STATIC_SAMPLER_DESC staticSamplerDesc { CreateStaticSamplerDesc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP) };
    
    D3D12_FEATURE_DATA_ROOT_SIGNATURE d3d12FeatureDataRootSignature = GetFeatureRootSignatureHighestVerSupported();
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = d3d12FeatureDataRootSignature.HighestVersion;
    rootSignatureDesc.Desc_1_1.NumParameters = rootParametersCount;
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = staticSamplerDescsCount;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSamplerDesc;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlobPtr signature;
    AssertIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, nullptr));
    AssertIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
    m_rootSignature->SetName(L"Root Signature");
    
    // Define the vertex input layout.
    const size_t inputElementDescsCount = 2;
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[inputElementDescsCount] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
        
    const UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
    const char* vertexShaderSrc = R"(   
                                        struct Transformations
                                        {
                                            float4x4 worldCamProj;
                                        };
                                        ConstantBuffer<Transformations> transformations : register(b0, space0);

                                        struct Interpolators
                                        {
                                            float4 m_position : SV_POSITION;
                                            float2 m_uv : TEXCOORD;
                                        };
    
                                        Interpolators VertexShaderMain(float4 position : POSITION, float2 uv : TEXCOORD)
                                        {
                                            Interpolators result;
                                            result.m_position = mul(position, transformations.worldCamProj);
                                            result.m_uv = uv;
                                            return result;
                                        }
                                    )";
    ID3DBlobPtr vertexShader = CompileShader(vertexShaderSrc, "VertexShaderMain", "vs_5_1", compileFlags);

    const char* pixelShaderSrc = R"(
                                        struct Interpolators
                                        {
                                            float4 m_position : SV_POSITION;
                                            float2 m_uv : TEXCOORD;
                                        };
    
                                        Texture2D colorTexture : register(t0);
                                        SamplerState linearSampler : register(s0);
    
                                        float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
                                        {
                                            return colorTexture.Sample(linearSampler, interpolators.m_uv);
                                        }
                                    )";
    ID3DBlobPtr pixelShader = CompileShader(pixelShaderSrc, "PixelShaderMain", "ps_5_1", compileFlags);

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs , inputElementDescsCount };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CreateDefaultRasterizerState();
    psoDesc.BlendState = CreateDefaultBlendState();
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    AssertIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_defaultPSO)));
    m_defaultPSO->SetName(L"Default PSO");
}

D3D12_FEATURE_DATA_ROOT_SIGNATURE D3D12SimpleMaterial::GetFeatureRootSignatureHighestVerSupported()
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

    // This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
    featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

    if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
        featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;

    return featureData;
}
