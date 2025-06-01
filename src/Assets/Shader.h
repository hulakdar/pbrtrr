#pragma once

enum ShaderType
{
	ePixelShader,
	eVertexShader,
	eGeometryShader,
	eHullShader,
	eDomainShader,
	eComputeShader,
	eShaderTypeCount,
};

struct ShaderReflectionData
{
	u8 NumCBVs;
	u8 NumSRVs;
	u8 NumUAVs;
	u8 NumSamplers;
};

struct ShaderReflectionExtraDataCS
{
	u8 GroupsizeX;
	u8 GroupsizeY;
	u8 GroupsizeZ;
};

struct ShaderResourceBinding
{
	u32 NameExtraOffset;
	u8 BindIndex;
	u8 BindCount;
};