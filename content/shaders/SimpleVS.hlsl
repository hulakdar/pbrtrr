#include "Common.h"

struct ModelViewProjection
{
    matrix MVP;
};
 
ConstantBuffer<ModelViewProjection> ModelViewProjectionCB : register(b0);

struct VertexPosColor
{
    float3 Position : POSITION;
    float3 Color    : COLOR;
};

VertexShaderOutput main(VertexPosColor IN)
{
    VertexShaderOutput OUT;
 
    OUT.Position = mul(ModelViewProjectionCB.MVP, float4(IN.Position, 1.0f));
    OUT.Color = float4(IN.Color, 1.0f);
 
    return OUT;
}

