struct VertexShaderOutput
{
	float4 Color : COLOR;
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

cbuffer PushConstants : register(b0);
{
	float2 ScreenSize;
	int TexID;
	int Flags;
};

VertexShaderOutput MainVS(
	float2 position : POSITION,
	float2 uv : TEXCOORD,
	float4 color : COLOR
)
{
	VertexShaderOutput output;

	float2 Scaled = (position / PushConstants.ScreenSize);
	float2 Final = Scaled * float2(2, -2) + float2(-1, 1);

	output.Color = color;
	output.Position = float4(Final, 0, 1);
	output.UV = uv;

	return output;
}

Texture2D<float4>     SRVs[] : register(t0);
SamplerState	PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

float4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	if ((PushConstants.Flags & 0x80000000) != 0)
	{
		return In.Color * SRVs[PushConstants.TexID].SampleLevel(BilinearSampler, In.UV, PushConstants.Flags & 0xff);
	}
	else
	{
		return In.Color * SRVs[PushConstants.TexID].SampleLevel(PointSampler, In.UV, PushConstants.Flags & 0xff);
	}
}

