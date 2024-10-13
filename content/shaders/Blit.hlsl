struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

VertexShaderOutput MainVS(uint id : SV_VertexID)
{
	VertexShaderOutput output;

	float2 position = float2(-1.0, -3.0);
	position += lerp(float2(0,0), float2(0,4), id == 1);
	position += lerp(float2(0,0), float2(4,4), id == 2);

	output.Position = float4(position, 0, 1);
	output.UV = position.xy * float2(0.5, -0.5) + 0.5;

	return output;
}

Texture2D<float4>      Input : register(t0);
SamplerState	PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

float4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	return Input.Sample(BilinearSampler, In.UV);
}

