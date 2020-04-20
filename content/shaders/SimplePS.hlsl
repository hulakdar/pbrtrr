#include "Common.h"

float4 main( VertexShaderOutput IN ) : SV_Target
{
    return IN.Color;
}
