struct Output
{
    float4 color : SV_Target0;
};

struct Input
{
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

[[vk::binding(0, 2)]]
Texture2D    Texture : register(t0);
[[vk::binding(0, 2)]]
SamplerState Sampler : register(s0);

Output main(Input input)
{
    Output output;

    float4 tex_color = Texture.Sample(Sampler, input.texcoord);
    output.color = tex_color * input.color;
    return output;
}
