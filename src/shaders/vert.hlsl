struct Output
{
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
    float4 position : SV_Position;
};

struct Input
{
    float3 position : POSITION;
    float2 texcoord : TEXCOORD0;
    float4 color    : TEXCOORD1;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 1.0);
    output.texcoord = input.texcoord;
    output.color    = input.color;
    return output;
}
