#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"                                                \
              "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX),"                                                 \
              "DescriptorTable( CBV(b0), SRV(t0, numDescriptors = 2), visibility = SHADER_VISIBILITY_PIXEL),"   \
              "StaticSampler(s0, "                                                                              \
                             "filter = FILTER_COMPARISON_ANISOTROPIC, "                                         \
                             "addressU = TEXTURE_ADDRESS_BORDER, "                                              \
                             "addressV = TEXTURE_ADDRESS_BORDER, "                                              \
                             "addressW = TEXTURE_ADDRESS_BORDER, "                                              \
                             "maxAnisotropy = 1,"                                                               \
                             "comparisonFunc = COMPARISON_LESS,"                                                \
                             "borderColor = STATIC_BORDER_COLOR_OPAQUE_BLACK,"                                  \
                             "visibility = SHADER_VISIBILITY_PIXEL)"
struct ShadingData
{
    float4x4 m_worldCamProj;
    float4x4 m_worldLightProj[2];
    float4x4 m_normalWorld;
    float4 m_lightDirection[2];
};
ConstantBuffer<ShadingData> g_shadingData : register(b0);

struct Interpolators
{
    float4 m_position : SV_POSITION;
    float4 m_normal : NORMAL;
    float4 m_positionLS0 : TEXCOORD0;
    float4 m_positionLS1 : TEXCOORD1;
    float2 m_uv : TEXCOORD2;
    float3 m_lightDirection0 : TEXCOORD3;
    float3 m_lightDirection1 : TEXCOORD4;
};

Texture2D shadowMap0    : register(t0);
Texture2D shadowMap1    : register(t1);
SamplerComparisonState cmpLessSampler : register(s0);

struct MaterialData
{
    float4 m_fixedColor;
};
ConstantBuffer<MaterialData> g_MaterialData : register(b0);

float SampleShadowMap(float3 positionLS, Texture2D shadowMap)
{
    float3 lightProjectedCoords = positionLS;
    float2 shadowMapUV = lightProjectedCoords.xy * 0.5f + 0.5f;
    shadowMapUV.y = 1.0f - shadowMapUV.y;
    const float depthBias = 0.0001f;
    return shadowMap.SampleCmpLevelZero(cmpLessSampler, shadowMapUV, lightProjectedCoords.z - depthBias).r;
}

Interpolators VertexShaderMain(float4 position : POSITION, 
                                float2 uv : TEXCOORD,
                                float4 normal : NORMAL)
{
    Interpolators result;
    result.m_position = mul(position, g_shadingData.m_worldCamProj);
    result.m_positionLS0 = mul(position, g_shadingData.m_worldLightProj[0]);
    result.m_positionLS1 = mul(position, g_shadingData.m_worldLightProj[1]);
    result.m_normal = normalize(mul(normal, g_shadingData.m_normalWorld));
    result.m_lightDirection0 = normalize(g_shadingData.m_lightDirection[0].xyz);
    result.m_lightDirection1 = normalize(g_shadingData.m_lightDirection[1].xyz);

    result.m_uv = uv;
    return result;
}


float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
{
    const float shadow0 = SampleShadowMap(interpolators.m_positionLS0, shadowMap0);
    const float shadow1 = SampleShadowMap(interpolators.m_positionLS1, shadowMap1);
    const float dotLV0 = saturate(dot(interpolators.m_lightDirection0, interpolators.m_normal));
    const float dotLV1 = saturate(dot(interpolators.m_lightDirection1, interpolators.m_normal));
    const float4 color = g_MaterialData.m_fixedColor;

    const float4 ambient = color * 0.3f;
    return color * (dotLV0 * shadow0 + dotLV1 * shadow1) + ambient;
}