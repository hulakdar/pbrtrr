struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

struct PerFrameConstants
{
	float  Scale;
	float  Padding;
	float2 ScreenSize;
};

VertexShaderOutput MainVS(
	float4 position : POSITION,
	float2 uv : TEXCOORD)
{
	VertexShaderOutput output;

	output.Position = position;
	output.UV = uv;

	return output;
}

Texture2D<float4>    LenaStd : register(t0);
SamplerState	PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

float4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	return LenaStd.Sample(BilinearSampler, In.UV);
}

