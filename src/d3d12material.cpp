#include "D3D12Material.h"

// C includes
#include <cassert>
#include <sstream>

// directx includes
#include <d3d12.h>
#include <d3dcompiler.h>

// project includes
#include "utils.h"
#include "d3d12descriptorheap.h"
#include "d3d12gpu.h"
#include "d3d12utils.h"

using namespace D3D12Basics;

D3D12Material::D3D12Material(D3D12Gpu& gpu)
{
    CreatePSO(gpu);
}

D3D12Material::~D3D12Material()
{}

void D3D12Material::CreatePSO(D3D12Gpu& gpu)
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
    D3D12_STATIC_SAMPLER_DESC staticSamplerDesc{ CreateStaticLinearSamplerDesc() };

    D3D12_FEATURE_DATA_ROOT_SIGNATURE d3d12FeatureDataRootSignature = D3D12_FEATURE_DATA_ROOT_SIGNATURE{ D3D_ROOT_SIGNATURE_VERSION_1_1 };
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Version = d3d12FeatureDataRootSignature.HighestVersion;
    rootSignatureDesc.Desc_1_1.NumParameters = rootParametersCount;
    rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
    rootSignatureDesc.Desc_1_1.NumStaticSamplers = staticSamplerDescsCount;
    rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSamplerDesc;
    rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlobPtr signature;
    AssertIfFailed(D3D12SerializeVersionedRootSignature(&rootSignatureDesc, &signature, nullptr));
    m_rootSignature = gpu.CreateRootSignature(signature, L"D3D12Material Root Signature");
    assert(m_rootSignature);

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
    assert(vertexShader);

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
    assert(pixelShader);

    // Describe and create the graphics pipeline state object (PSO).
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputElementDescs , inputElementDescsCount };
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
    psoDesc.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };
    psoDesc.RasterizerState = CreateDefaultRasterizerState();
    psoDesc.BlendState = CreateDefaultBlendState();
    psoDesc.DepthStencilState.DepthEnable = TRUE;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;
    m_pso = gpu.CreatePSO(psoDesc, L"D3D12Material PSO");
    assert(m_pso);
}
