Texture2D<float4>    Input : register(t0);
RWTexture2D<float4>    Output : register(u0);

SamplerState BilinearSampler : register(s0);

[numthreads(1, 1, 1)]
void Main( uint3 DTid : SV_DispatchThreadID )
{
	uint Width, Height;
	Input.GetDimensions(Width, Height);
	float2 UV = float2(DTid.x / (float)Width, DTid.y / (float)Height);
	Output[DTid.xy] = Input.Gather(BilinearSampler, UV);
}