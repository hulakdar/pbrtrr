struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

struct PerFrameConstants
{
	float4x4 MVP;
};

ConstantBuffer<PerFrameConstants> Constants : register(b0);

VertexShaderOutput MainVS(
	float4 position : POSITION,
	float2 uv : TEXCOORD)
{
	VertexShaderOutput output;

	output.Position = mul(Constants.MVP, position);
	output.UV = uv;

	return output;
}

Texture2D<float4>    Diffuse : register(t0);
SamplerState	PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

float4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	float4 Color = Diffuse.Sample(BilinearSampler, In.UV);
	clip(Color.a);
	return Color;
}

