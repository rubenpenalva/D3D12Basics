#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"                                                \
              "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX),"                                                 \
              "DescriptorTable( CBV(b0), visibility = SHADER_VISIBILITY_PIXEL)"                                 \

struct ShadingData
{
    float4x4 m_worldCamProj;
};
ConstantBuffer<ShadingData> g_shadingData : register(b0);

struct Interpolators
{
    float4 m_position : SV_POSITION;
};

struct MaterialData
{
    float4 m_fixedColor;
};
ConstantBuffer<MaterialData> g_MaterialData : register(b0);

Interpolators VertexShaderMain(float4 position : POSITION)
{
    Interpolators result;
    result.m_position = mul(position, g_shadingData.m_worldCamProj);
    return result;
}

float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
{
    return g_MaterialData.m_fixedColor;
}