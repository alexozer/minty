struct Output
{
    float4 color : SV_Target0;
};

struct Input
{
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

Texture2D    Texture : register(t0);
SamplerState Sampler : register(s0);

Output main(Input input)
{
    Output output;
    output.color = Texture.Sample(Sampler, input.texcoord);
    return output;
}
