#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"                    \
              "DescriptorTable( SRV(t0), visibility = SHADER_VISIBILITY_PIXEL),"    \
              "StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL)"


struct Interpolators
{
    float4 m_position : SV_POSITION;
    float2 m_uv : TEXCOORD;
};

Interpolators VertexShaderMain(float4 position : POSITION, float2 uv : TEXCOORD)
{
    Interpolators result;
    result.m_position = position;
    result.m_uv = uv;
    return result;
}

Texture2D colorTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
{
    return 1000.0f * colorTexture.Sample(linearSampler, interpolators.m_uv).r;
}