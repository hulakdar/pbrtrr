struct VertexShaderOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD;
};

cbuffer PushConstants : register(b0)
{
	float4x4 MVP;
	int diffuseTextureIndex;
	float lodClampIndex;
	uint mouseX;
	uint mouseY;
};

VertexShaderOutput MainVS(
	float4 position : POSITION,
	half3 normal   : NORMAL,
	half4 color    : COLOR,
	float2 uv       : TEXCOORD0)
{
	VertexShaderOutput output;

	output.Position = mul(PushConstants.MVP, position);
	output.UV = uv;

	return output;
}

Texture2D<half4>         SRVs[]          : register(t0);
RWStructuredBuffer<uint> SamplerFeedback : register(u0);
RWStructuredBuffer<uint> Picking          : register(u1);

SamplerState	PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

half4 MainPS(VertexShaderOutput In) : SV_TARGET
{
	float l = SRVs[PushConstants.diffuseTextureIndex].CalculateLevelOfDetail(BilinearSampler, In.UV);

	l = clamp(l, 0, 16);
	uint CrossLaneMin = WaveActiveMin(l) * 1024.f;
	if (WaveIsFirstLane())
	{
		InterlockedMin(SamplerFeedback[PushConstants.diffuseTextureIndex], CrossLaneMin);
	}

	half4 Color = SRVs[PushConstants.diffuseTextureIndex].Sample(BilinearSampler, In.UV, int2(0,0), PushConstants.lodClampIndex);
	clip(Color.a - 0.5);

	if (all(uint2(In.Position.xy) == uint2(PushConstants.mouseX, PushConstants.mouseY)))
	{
		uint DepthAndIndex = PushConstants.diffuseTextureIndex;
		DepthAndIndex |= uint(In.Position.z * 65535.f) << 16;
		InterlockedMax(Picking[0], DepthAndIndex);
	}

	return Color;
}

