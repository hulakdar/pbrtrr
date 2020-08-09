struct VertexShaderOutput
{
	float4 Color : COLOR;
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

struct PerFrameConstants
{
	float2 ScreenSize;
};

ConstantBuffer<PerFrameConstants> Constants : register(b0);

VertexShaderOutput MainVS(
	float2 position : POSITION,
	float2 uv : TEXCOORD,
	float4 color : COLOR
)
{
	VertexShaderOutput output;

	float2 Scaled = (position / Constants.ScreenSize);
	float2 Final = Scaled * float2(2, -2) + float2(-1, 1);

	output.Color = color;
	output.Position = float4(Final, 0, 1);
	output.UV = uv;

	return output;
}

Texture2D<float4>    Texture : register(t0);
SamplerState BilinearSampler : register(s0);

float4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	return In.Color * Texture.Sample(BilinearSampler, In.UV).x;
}

