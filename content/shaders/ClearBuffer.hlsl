cbuffer PushConstants : register(b0)
{
    uint Value;
    uint Size;
};

RWStructuredBuffer<uint> OutBuffer : register(u0);

[numthreads(256,1,1)]
void MainCS(uint3 ThreadID : SV_DispatchThreadID)
{
    if (ThreadID.x < Size)
    {
        OutBuffer[ThreadID.x] = Value;
    }
}