#define MyRS1 "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ),"    \
              "CBV(b0, visibility = SHADER_VISIBILITY_VERTEX)"

struct Transformation
{
    float4x4 worldCamProj;
};
ConstantBuffer<Transformation> g_transformation : register(b0);

float4  VertexShaderMain(float4 position : POSITION) : SV_POSITION
{
    return mul(position, g_transformation.worldCamProj);
}