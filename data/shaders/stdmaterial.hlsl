#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"                                                \
              "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX),"                                                 \
              "DescriptorTable( SRV(t0, numDescriptors = 4), visibility = SHADER_VISIBILITY_PIXEL),"            \
              "StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL),"                                        \
              "StaticSampler(s1, "                                                                              \
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
    float4x4    m_worldCamProj;
    float4x4    m_worldLightProj[2];
    float4x4    m_normalWorld;
    float4      m_lightDirection[2];
};
ConstantBuffer<ShadingData> g_shadingData : register(b0);

Texture2D colorTexture : register(t0);
Texture2D normalTexture : register(t1);
// TODO specular
// Texture2D specularTexture : register(t2);
Texture2D shadowMap0    : register(t2);
Texture2D shadowMap1    : register(t3);
SamplerState linearSampler : register(s0);
SamplerComparisonState cmpLessSampler : register(s1);

struct Interpolators
{
    float4 m_position : SV_POSITION;
    float3 m_positionLS0 : TEXCOORD0;
    float3 m_positionLS1 : TEXCOORD1;
    float2 m_uv : TEXCOORD2;
    float3 m_lightDirectionTS0 : TEXCOORD3;
    float3 m_lightDirectionTS1 : TEXCOORD4;
};

float SampleShadowMap(float3 positionLS, Texture2D shadowMap)
{
    float3 lightProjectedCoords = positionLS;
    float2 shadowMapUV = lightProjectedCoords.xy * 0.5f + 0.5f;
    shadowMapUV.y = 1.0f - shadowMapUV.y;
    const float depthBias = 0.0001f;
    return shadowMap.SampleCmpLevelZero(cmpLessSampler, shadowMapUV, lightProjectedCoords.z - depthBias).r;
}

float3 SampleNormalMap(float2 uv)
{
    return normalize(normalTexture.Sample(linearSampler, uv) * 2.0f - 1.0f).xyz;
}

Interpolators VertexShaderMain(float4 position : POSITION, 
                                float2 uv : TEXCOORD,
                                float4 normal : NORMAL, 
                                float4 tangent : TANGENT,
                                float4 binormal : BINORMAL)
{
    Interpolators result;
    result.m_position = mul(position, g_shadingData.m_worldCamProj);
    result.m_positionLS0 = mul(position, g_shadingData.m_worldLightProj[0]).xyz;
    result.m_positionLS1 = mul(position, g_shadingData.m_worldLightProj[1]).xyz;

    const float4 normalWS = mul(normal, g_shadingData.m_normalWorld);
    const float4 tangentWS = mul(tangent, g_shadingData.m_normalWorld);
    const float4 binormalWS = mul(binormal, g_shadingData.m_normalWorld);
    float4x4 worldToTangentSpace = {tangentWS, binormalWS, normalWS, float4(0,0,0,1)};
    worldToTangentSpace = transpose(worldToTangentSpace);
    result.m_lightDirectionTS0 = normalize(mul(float4(g_shadingData.m_lightDirection[0].xyz, 1.0f), worldToTangentSpace)).xyz;
    result.m_lightDirectionTS1 = normalize(mul(float4(g_shadingData.m_lightDirection[1].xyz, 1.0f), worldToTangentSpace)).xyz;

    result.m_uv = uv;
    return result;
}

float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
{
    const float4 color = colorTexture.Sample(linearSampler, interpolators.m_uv);
    const float shadow0 = SampleShadowMap(interpolators.m_positionLS0, shadowMap0);
    const float shadow1 = SampleShadowMap(interpolators.m_positionLS1, shadowMap1);
    const float3 normalTS = SampleNormalMap(interpolators.m_uv);

    const float dotLV0 = saturate(dot(interpolators.m_lightDirectionTS0[0], normalTS));
    const float dotLV1 = saturate(dot(interpolators.m_lightDirectionTS1[1], normalTS));

    const float4 ambient = color * 0.3f;
    return color * (dotLV0 * shadow0 + dotLV1 * shadow1) + ambient;
}
