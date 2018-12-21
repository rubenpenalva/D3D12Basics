#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"                                                \
              "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX),"                                                  \
              "DescriptorTable( SRV(t0, numDescriptors = 1), visibility = SHADER_VISIBILITY_PIXEL),"            \
              "StaticSampler(s0, visibility = SHADER_VISIBILITY_PIXEL)"                                         

struct Transformation
{
    float4x4 worldCamProj;
};
ConstantBuffer<Transformation> transformation : register(b0);

struct Interpolators
{
    float4 m_position   : SV_POSITION;
    float2 m_uv         : TEXCOORD;
    float4 m_color      : COLOR;
};

Interpolators VertexShaderMain(float2 position : POSITION, float2 uv : TEXCOORD, float4 color : COLOR)
{
    Interpolators result;
    result.m_position = mul(transformation.worldCamProj, float4(position, 0.0f, 1.0f));
    result.m_color = color;
    result.m_uv = uv;
    return result;
}

Texture2D colorTexture : register(t0);
SamplerState linearSampler : register(s0);

float4 PixelShaderMain(Interpolators interpolators) : SV_TARGET
{
    return interpolators.m_color * colorTexture.Sample(linearSampler, interpolators.m_uv);
}

