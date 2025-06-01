#include "Assets/Shader.generated.h"
#include "Containers/ComPtr.generated.h"

#include <dxcapi.h>
#include <d3d12shader.h>

static TComPtr<IDxcUtils>    gDxcUtils;
static TComPtr<IDxcCompiler3> gDxcCompiler;
static TComPtr<IDxcIncludeHandler> gIncludeHandler;

namespace
{
	void PrintBlob(IDxcBlob* ErrorBlob)
	{
		printf("Shader error: %.*s", (int)ErrorBlob->GetBufferSize(), (char*)ErrorBlob->GetBufferPointer());
		OutputDebugStringA((char*)ErrorBlob->GetBufferPointer());
		//std::string_view Str((char*)ErrorBlob->GetBufferPointer(), ErrorBlob->GetBufferSize());
		//Debug::Print(Str);
	}
}

void InitShaderCompiler()
{
	DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(gDxcUtils.GetAddressOf()));
	DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(gDxcCompiler.GetAddressOf()));
	gDxcUtils->CreateDefaultIncludeHandler(gIncludeHandler.GetAddressOf());
}

StringView GetTargetVersionFromType(ShaderType Type)
{
	static StringView TargetVersions[] = {
		"ps",
		"vs",
		"",
		"",
		"",
		"cs",
		"",
		"",
		"",
		"",
	};
	if (Type != eShaderTypeCount)
	{
		return TargetVersions[Type];
	}
	DEBUG_BREAK();
	return "";
}

ShaderType GetShaderTypeFromEntryPoint(StringView EntryPoint)
{
	if (EndsWith(EntryPoint, "VS"))
	{
		return eVertexShader;
	}
	else if (EndsWith(EntryPoint, "PS"))
	{
		return ePixelShader;
	}
	else if (EndsWith(EntryPoint, "CS"))
	{
		return eComputeShader;
	}
	return eShaderTypeCount;
}

TComPtr<IDxcBlob> CompileShader(StringView FileName, StringView EntryPoint, ID3D12ShaderReflection** Reflection)
{
	ShaderType Type = GetShaderTypeFromEntryPoint(EntryPoint);
	StringView TargetType = GetTargetVersionFromType(Type);

	String TargetVersion = StringFromFormat(
		"%.*s_%d_%d\0\0\0\0",
		TargetType.size(), TargetType.data(),
		6, 2 //#TODO do not hardcode this
	);
	WString Target = ToWide(TargetVersion);

	FileMapping ShaderMap = MapFile(FileName);
	StringView Shader = GetAsStringView(ShaderMap);
	
	if (Shader.rfind(EntryPoint) == Shader.npos)
	{
		UnmapFile(ShaderMap);
		return {nullptr};
	}

	DxcBuffer Source{};
	Source.Ptr = Shader.data();
	Source.Size = Shader.size();

	WString File = ToWide(String(FileName));
	WString Entry = ToWide(String(EntryPoint));

	PCWSTR Arguments[] = {
		File.c_str(),
		L"-T", Target.c_str(),
		L"-E", Entry.c_str(),
		L"-Zi",
		L"-Qembed_debug",
		L"-Qstrip_reflect",
		L"-Fre", L"reflection",
	};

	TComPtr<IDxcResult> OperationResult;
	HRESULT HR = gDxcCompiler->Compile(
		&Source,
		Arguments, ArrayCount(Arguments),
		gIncludeHandler.Get(),
		IID_PPV_ARGS(OperationResult.GetAddressOf())
	);

	UnmapFile(ShaderMap);

	if (SUCCEEDED(HR))
	{
		OperationResult->GetStatus(&HR);
	}
	else
	{
		DEBUG_BREAK();
		return { nullptr };
	}

	TComPtr<IDxcBlobEncoding> ErrorBlob;
	OperationResult->GetErrorBuffer(&ErrorBlob);

	if (ErrorBlob->GetBufferSize())
	{
		PrintBlob(ErrorBlob.Get());
	}

	if (!SUCCEEDED(HR))
	{
		DEBUG_BREAK();
		return { nullptr };
	}

	TComPtr<IDxcBlob> Result;
	OperationResult->GetResult(&Result);
	if (Reflection)
	{
		TComPtr<IDxcBlob> Refl;
		OperationResult->GetOutput(DXC_OUT_REFLECTION, IID_PPV_ARGS(Refl.GetAddressOf()), nullptr);
		DxcBuffer ReflBlob{};
		ReflBlob.Ptr = Refl->GetBufferPointer();
		ReflBlob.Size = Refl->GetBufferSize();
		gDxcUtils->CreateReflection(&ReflBlob, IID_PPV_ARGS(Reflection));
	}
	return Result;
}
