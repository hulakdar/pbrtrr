Texture2D<float4>    Input : register(t0);
RWTexture2D<float4>    Output : register(u0);

SamplerState PointSampler : register(s0);
SamplerState BilinearSampler : register(s1);

#define WIDTH 4

[numthreads(1, 1, 1)]
void Main( uint3 DTid : SV_DispatchThreadID )
{
	uint OWidth, OHeight;
	Output.GetDimensions(OWidth, OHeight);

	uint IWidth, IHeight;
	Input.GetDimensions(IWidth, IHeight);

	float2 InputHalfInvSize = float2(1.f/IWidth, 1.f/IHeight) * 0.5;

	float2 UV = float2(
		DTid.x / (float)OWidth * WIDTH,
		DTid.y / (float)OHeight * WIDTH
	) + InputHalfInvSize;

	for (int i = 0; i < WIDTH; ++i)
    {
    	for (int j = 0; j < WIDTH; ++j)
        {
            Output[DTid.xy * WIDTH + uint2(i,j)] = Input.SampleLevel(
				BilinearSampler,
                UV + InputHalfInvSize * float2(2 * i,2 * j),
				0
			);
        }
    }
}