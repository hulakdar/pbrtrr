#pragma once

#include <Transcode.hlsl>

float2 octPEncode(in float3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    float2 result = v.xy * (1.0/l1norm);
    if (v.z < 0.0) {
        result = (1.0 - abs(result.yx)) * signNotZero(result.xy);
    }
    return result;
}

float3 octPEncode3Components(in float3 v) {
    float l1norm = abs(v.x) + abs(v.y) + abs(v.z);
    v *= (1.0/l1norm);
    if (v.z < 0.0) {
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    }
    return v;
}

float3 finalDecode(float x, float y) {
    float3 v = float3(x, y, 1.0 - abs(x) - abs(y));
    if (v.z < 0) {
        v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    }
    return normalize(v);
}


float3 decode2ComponentSnorm8(in uint x_int, in uint y_int) {
    float x = unpackSnorm8(x_int);
    float y = unpackSnorm8(y_int);
    return finalDecode(x,y);
}

float3 decodeSnorm8(in uint p) {
    uint x_int, y_int;
    unpack2Norm8s(p, x_int, y_int);
    return decode2ComponentSnorm8(x_int, y_int);
}



float3 decode2ComponentSnorm12(in uint x_int, in uint y_int) {
    float x = unpackSnorm12(x_int);
    float y = unpackSnorm12(y_int);
    return finalDecode(x,y);
}

float3 decodeSnorm12(in uint p) {
    uint x_int, y_int;
    unpack2Norm12s(p, x_int, y_int);
    return decode2ComponentSnorm12(x_int, y_int);
}



float3 decode2ComponentSnorm16(in uint x_int, in uint y_int) {
    float x = unpackSnorm16(x_int);
    float y = unpackSnorm16(y_int);
    return finalDecode(x,y);
}

float3 decodeSnorm16(in uint p) {
    uint x_int, y_int;
    unpack2Norm16s(p, x_int, y_int);
    return decode2ComponentSnorm16(x_int, y_int);
}



float3 decode16(in float2 p) {
    return finalDecode(p.x, p.y);
}

float3 decode32(in float2 p) {
    return finalDecode(p.x, p.y);
}

float3 decode24(in float3 p) {
    float2 v = twoSnorm12sEncodedAsUVec3InVec3FormatToVec2(p);
    return finalDecode(v.x, v.y);
}





void evaluateTrial(in uint trial, in float3 trialResult, in float3 normv, inout float closeness, inout uint optimal) {  
    float trialCloseness = dot(trialResult, normv);
    if(closeness < trialCloseness) {
        closeness   = trialCloseness;
        optimal     = trial;
    }
}

#define EVALUATE_FROM_TWO_COMPONENTS 0

void evaluateTrial2Components16(in uint x_trial, in uint y_trial, in float3 trialResult, in float3 normv, inout float closeness, inout uint optimal) {  
    float trialCloseness = dot(trialResult, normv);
    if(closeness < trialCloseness) {
        closeness   = trialCloseness;
        optimal     = pack2Norm8s(x_trial, y_trial);
    }
}

uint encodeIntoSnorm8s(float3 v) {
    float3 normv = normalize(v);
    float2 uv = octPEncode(v);
    uint x = packSnormFloor8(uv.x);
    uint y = packSnormFloor8(uv.y);

    uint optimal;

    float closeness = 0.0;

    float3 trialResult = float3(0,0,0);

    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
#               if EVALUATE_FROM_TWO_COMPONENTS
                uint x_trial = uint(int(x) + i);
                uint y_trial = uint(int(y) + j);
                trialResult = decode2ComponentSnorm8(x_trial, y_trial);
                evaluateTrial2Components16(x_trial, y_trial, trialResult, normv, closeness, optimal);

#               else
                uint trial = pack2Norm8s(uint(int(x) + i), (int(y) + j));
                trialResult = decodeSnorm8(trial);
                evaluateTrial(trial, trialResult, normv, closeness, optimal);
                    
#               endif

            }
    }
    return optimal;
}

float2 encodeIntoSnorm8sStoredAsVec2(float3 v) {
    float3 normv = normalize(v);
    float2 s = octPEncode(normv);
    s = floor(clamp(s, -1.0, 1.0) * 127 ) * ( 1.0 / 127 );

    // Prime the loop
    float2 bestRepresentation = s;
    float highestCosine = dot(finalDecode(s.x, s.y), normv);
    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                // This branch will be evaluated at compile time
                if ( (i != 0) || (j != 0) ) {
                    float2 candidate = float2(i,j) * (1.0 / 127 ) + s;
                    float3 roundTrip = finalDecode(candidate.x, candidate.y);

                    float cosine = dot(roundTrip, normv);
                    if (cosine > highestCosine) {
                        bestRepresentation = candidate;
                        highestCosine      = cosine;
                    }
                }
            }
    }
    return bestRepresentation;
}


void evaluateTrial2Components24(in uint x_trial, in uint y_trial, in float3 trialResult, in float3 normv, inout float closeness, inout uint optimal) {  
    float trialCloseness = dot(trialResult, normv);
    if(closeness < trialCloseness) {
        closeness   = trialCloseness;
        optimal     = pack2Norm12s(x_trial, y_trial);
    }
}

uint encodeIntoSnorm12s(float3 v) {
    float3 normv = normalize(v);
    float2 uv = octPEncode(v);
    uint x = packSnormFloor12(uv.x);
    uint y = packSnormFloor12(uv.y);

    uint optimal;

    float closeness = 0.0;

    float3 trialResult = float3(0,0,0);

    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
#               if EVALUATE_FROM_TWO_COMPONENTS
                uint x_trial = uint(int(x) + i);
                uint y_trial = uint(int(y) + j);
                trialResult = decode2ComponentSnorm12(x_trial, y_trial);
                evaluateTrial2Components24(x_trial, y_trial, trialResult, normv, closeness, optimal);

#               else
                uint trial = pack2Norm12s(uint(int(x) + i), (int(y) + j));
                trialResult = decodeSnorm12(trial);
                evaluateTrial(trial, trialResult, normv, closeness, optimal);
                    
#               endif

            }
    }
    return optimal;
}

float2 encodeIntoSnorm12sStoredAsVec2(float3 v) {
    float3 normv = normalize(v);
    float2 s = octPEncode(normv);
    s = floor(clamp(s, -1.0, 1.0) * 2047 ) * ( 1.0 / 2047 );

    // Prime the loop
    float2 bestRepresentation = s;
    float highestCosine = dot(finalDecode(s.x, s.y), normv);
    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                // This branch will be evaluated at compile time
                if ( (i != 0) || (j != 0) ) {
                    float2 candidate = float2(i,j) * (1.0 / 2047 ) + s;
                    float3 roundTrip = finalDecode(candidate.x, candidate.y);

                    float cosine = dot(roundTrip, normv);
                    if (cosine > highestCosine) {
                        bestRepresentation = candidate;
                        highestCosine      = cosine;
                    }
                }
            }
    }
    return bestRepresentation;
}


void evaluateTrial2Components32(in uint x_trial, in uint y_trial, in float3 trialResult, in float3 normv, inout float closeness, inout uint optimal) {  
    float trialCloseness = dot(trialResult, normv);
    if(closeness < trialCloseness) {
        closeness   = trialCloseness;
        optimal     = pack2Norm16s(x_trial, y_trial);
    }
}

uint encodeIntoSnorm16s(float3 v) {
    float3 normv = normalize(v);
    float2 uv = octPEncode(v);
    uint x = packSnormFloor16(uv.x);
    uint y = packSnormFloor16(uv.y);

    uint optimal;

    float closeness = 0.0;

    float3 trialResult = float3(0,0,0);

    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
#               if EVALUATE_FROM_TWO_COMPONENTS
                uint x_trial = uint(int(x) + i);
                uint y_trial = uint(int(y) + j);
                trialResult = decode2ComponentSnorm16(x_trial, y_trial);
                evaluateTrial2Components32(x_trial, y_trial, trialResult, normv, closeness, optimal);

#               else
                uint trial = pack2Norm16s(uint(int(x) + i), (int(y) + j));
                trialResult = decodeSnorm16(trial);
                evaluateTrial(trial, trialResult, normv, closeness, optimal);
                    
#               endif

            }
    }
    return optimal;
}

float2 encodeIntoSnorm16sStoredAsVec2(float3 v) {
    float3 normv = normalize(v);
    float2 s = octPEncode(normv);
    s = floor(clamp(s, -1.0, 1.0) * 32767 ) * ( 1.0 / 32767 );

    // Prime the loop
    float2 bestRepresentation = s;
    float highestCosine = dot(finalDecode(s.x, s.y), normv);
    for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                // This branch will be evaluated at compile time
                if ( (i != 0) || (j != 0) ) {
                    float2 candidate = float2(i,j) * (1.0 / 32767 ) + s;
                    float3 roundTrip = finalDecode(candidate.x, candidate.y);

                    float cosine = dot(roundTrip, normv);
                    if (cosine > highestCosine) {
                        bestRepresentation = candidate;
                        highestCosine      = cosine;
                    }
                }
            }
    }
    return bestRepresentation;
}

float2 encode16(in float3 v) {
    return encodeIntoSnorm8sStoredAsVec2(v);
}


float2 encode32(in float3 v) {
    return encodeIntoSnorm16sStoredAsVec2(v);
}


float3 encode24(in float3 v) {
    return vec2To2Snorm12sEncodedAs3Unorm8sInVec3Format(encodeIntoSnorm12sStoredAsVec2(v));
}

