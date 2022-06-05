struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

VertexShaderOutput MainVS(float4 position : POSITION)
	
{
	VertexShaderOutput output;

	output.Position = position;
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

