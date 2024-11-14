
#define A_GPU
#define A_HLSL
#define A_HALF

#include "../../thirdparty/FidelityFX-SPD/ffx-spd/ffx_a.h"

#ifndef A_HALF
#define AH4 AF4
#define AH1 AF1
#endif

Texture2D<float4> imgSrc     : register(t0);
SamplerState BilinearSampler : register(s0);

struct SpdGlobalAtomicBuffer
{
    uint counter[6];
};
globallycoherent RWStructuredBuffer<SpdGlobalAtomicBuffer> spdGlobalAtomic : register(u0);
globallycoherent RWTexture2D<float4> imgDst[12] : register(u1);

cbuffer SpdConstants : register(b0)
{
   uint numWorkGroups;
   uint mips;
   float2 invInputSize;
};

AF4 SpdLoadSourceImage(ASU2 p, AU1 slice){
   AF2 textureCoord = p * invInputSize + invInputSize;
   return imgSrc.SampleLevel(BilinearSampler, textureCoord, 0);
}
AF4 SpdLoad(ASU2 p, AU1 slice){ return imgDst[5][p]; } // load from output MIP 5
void SpdStore(ASU2 p, AF4 value, AU1 mip, AU1 slice){ imgDst[mip][p] = value; }
AF4 SpdReduce4(AF4 v0, AF4 v1, AF4 v2, AF4 v3){return (v0+v1+v2+v3)*0.25;}

#ifndef A_HALF
groupshared AF4 spdIntermediate[16][16];
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value){spdIntermediate[x][y] = value;}
#else
groupshared AH4 spdIntermediate[16][16];
void SpdStoreIntermediate(AU1 x, AU1 y, AF4 value){spdIntermediate[x][y] = (AH4)value;}
#endif

AF4 SpdLoadIntermediate(AU1 x, AU1 y){return (AF4)spdIntermediate[x][y];}

AH4 SpdLoadIntermediateH(AU1 x, AU1 y){return (AH4)spdIntermediate[x][y];}
void SpdStoreIntermediateH(AU1 x, AU1 y, AH4 value){spdIntermediate[x][y] = value;}

AH4 SpdLoadSourceImageH(ASU2 p, AU1 slice){
   AF2 textureCoord = p * invInputSize + invInputSize;
   return AH4(imgSrc.SampleLevel(BilinearSampler, textureCoord, 0));
}
AH4 SpdLoadH(ASU2 tex, AU1 slice){return AH4(imgDst[5][tex]);}
void SpdStoreH(ASU2 pix, AH4 value, AU1 index, AU1 slice){imgDst[index][pix] = AF4(value);}
AH4 SpdReduce4H(AH4 v0, AH4 v1, AH4 v2, AH4 v3){return (v0+v1+v2+v3)*AH1(0.25);}

groupshared AU1 spdCounter;
void SpdIncreaseAtomicCounter(AU1 slice){InterlockedAdd(spdGlobalAtomic[0].counter[slice], 1, spdCounter);}
AU1 SpdGetAtomicCounter(){return spdCounter;}
void SpdResetAtomicCounter(AU1 slice){ spdGlobalAtomic[0].counter[slice] = 0; }

#define SPD_LINEAR_SAMPLER
#include "../../thirdparty/FidelityFX-SPD/ffx-spd/ffx_spd.h"

[numthreads(256,1,1)]
void MainCS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
#ifndef A_HALF
    SpdDownsample(
        AU2(WorkGroupId.xy), AU1(LocalThreadIndex),  
        AU1(mips), AU1(numWorkGroups), AU1(WorkGroupId.z)
    );
#else
    SpdDownsampleH(
		AU2(WorkGroupId.xy), AU1(LocalThreadIndex),  
        AU1(mips), AU1(numWorkGroups), AU1(WorkGroupId.z)
	);
#endif
}
