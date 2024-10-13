#include "Common.h"
#include "Render/RenderDebug.h"

#define A_CPU
#include "../thirdparty/FidelityFX-SPD/ffx-spd/ffx_a.h"
#include "../thirdparty/FidelityFX-SPD/ffx-spd/ffx_spd.h"
#undef A_CPU

#include "Assets/TextureDescription.generated.h"
#include "Assets/Pak.generated.h"
#include "Containers/Map.generated.h"
#include "Render/CommandListPool.generated.h"
#include "Render/RenderThread.generated.h"
#include "Render/RenderDX12.generated.h"
#include "Render/Texture.generated.h"
#include "Render/TransientResourcesPool.generated.h"
#include "System/Window.generated.h"
#include "System/GUI.generated.h"

#include <imgui.h>
#include <Tracy/TracyD3D12.hpp>

#include <stb/stb_image.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <Containers/StringView.h>

#include "AllDeclarations.h"
#include <WinPixEventRuntime/pix3.h>

TracyD3D12Ctx	gGraphicsProfilingCtx;
TracyD3D12Ctx	gComputeProfilingCtx;

Scene gScene;
Camera MainCamera;

TMap<u32, Shader> gMeshShaders;
void DrawDebugInfoMain()
{
	ImGui::ShowDemoWindow();
	static TArray<u8> MipIndeces;

	ImGui::Begin("Textures");
	auto& Textures = gScene.Textures;

	Generated::ToUI(&gScene);

	for (int i = 0; i < Textures.size(); ++i)
	{
		Generated::ToUI(&Textures[i]);
		ImGui::Text("Desired mip from feedback:%f", float(gScene.DesiredMips[Textures[i].TexData.SRV]) / 1024.f);

		int SelectedMipIndex = 0;
		if (i < MipIndeces.size())
		{
			SelectedMipIndex = MipIndeces[i];
		}

		int MipIndex = -1;
		if (Textures[i].TexData.NumMips > 1)
		{
			for (int j = 0; j < Textures[i].TexData.NumMips; ++j)
			{
				int frame_padding = -1;
				if (j == SelectedMipIndex)
				{
					frame_padding = 10;
				}

				if (ImGui::ImageButton(ImGuiTexIDWrapper(Textures[i].TexData.SRV, j), ImVec2(50, 50), ImVec2(0,0), ImVec2(1,1), frame_padding))
					MipIndex = j;
				ImGui::SameLine();
			}
			ImGui::NewLine();
		}
		if (MipIndex != -1)
		{
			if (MipIndeces.size() <= i)
				MipIndeces.resize(i + 1);

			MipIndeces[i] = MipIndex;
			SelectedMipIndex = MipIndex;
		}

        float my_tex_w = 400;
        float my_tex_h = 400;
		ImVec2 pos = ImGui::GetCursorScreenPos();
		ImGuiIO& io = ImGui::GetIO();
		ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);   // No tint
		ImVec4 border_col = ImVec4(1.0f, 1.0f, 1.0f, 0.5f); // 50% opaque white

		ImGui::Image(ImGuiTexIDWrapper(Textures[i].TexData.SRV, SelectedMipIndex, true), ImVec2(my_tex_w, my_tex_h));
		if (Textures[i].TexData.SRV == gScene.PickedSRV && ImGui::IsMouseClicked(ImGuiPopupFlags_MouseButtonMiddle))
		{
			ImGui::SetScrollHereY();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			float region_sz = 32.0f;
			float region_x = io.MousePos.x - pos.x - region_sz * 0.5f;
			float region_y = io.MousePos.y - pos.y - region_sz * 0.5f;
			float zoom = 4.0f;
			if (region_x < 0.0f) { region_x = 0.0f; }
			else if (region_x > my_tex_w - region_sz) { region_x = my_tex_w - region_sz; }
			if (region_y < 0.0f) { region_y = 0.0f; }
			else if (region_y > my_tex_h - region_sz) { region_y = my_tex_h - region_sz; }
			ImGui::Text("Min: (%.2f, %.2f)", region_x, region_y);
			ImGui::Text("Max: (%.2f, %.2f)", region_x + region_sz, region_y + region_sz);
			ImVec2 uv0 = ImVec2((region_x) / my_tex_w, (region_y) / my_tex_h);
			ImVec2 uv1 = ImVec2((region_x + region_sz) / my_tex_w, (region_y + region_sz) / my_tex_h);
			ImGui::Image(ImGuiTexIDWrapper(Textures[i].TexData.SRV, SelectedMipIndex, false), ImVec2(region_sz * zoom, region_sz * zoom), uv0, uv1, tint_col, border_col);
			ImGui::EndTooltip();
		}
	}
	ImGui::End();
}

TArray<TexID> TexturesStoppedStreaming;

void StreamingStopped(TexID ID)
{
	TexturesStoppedStreaming.push_back(ID);
}

void TickStreaming()
{
	TArray<D3D12_RESOURCE_BARRIER> Barriers;
	for (int i = 0; i < gScene.Textures.size(); ++i)
	{
		VirtualTexture& T = gScene.Textures[i];

		if (T.StreamingInProgress)
		{
			for (int i = 0; i < TexturesStoppedStreaming.size(); ++i)
			{
				if (TexturesStoppedStreaming[i].Value == T.TexData.ID.Value)
				{
					T.StreamingInProgress = false;
					TexturesStoppedStreaming.erase_unsorted(&TexturesStoppedStreaming[i]);
					break;
				}
			}
		}

		if (T.NumStreamedMips == 0 || T.StreamingInProgress || T.NumTilesForPacked == u16(-1))
		{
			continue;
		}

		u32 DesiredMip = AlignDown<u32>(gScene.DesiredMips[T.TexData.SRV], 1024) / 1024;
		if (DesiredMip < T.NumStreamedMips - T.NumStreamedIn)
		{
			T.StreamingInProgress = 1;

			i32 NextMip = (i32)T.NumStreamedMips - (i32)T.NumStreamedIn - 1;
			CHECK(NextMip >= 0);

			const PakItem* TextureItem = FindItem(gScene.FileReader, StringFromFormat("___Texture_%d_%d", i, NextMip));
			CHECK(TextureItem);
			CHECK(TextureItem->UncompressedDataSize < 0);

			Barriers.push_back() = CD3DX12_RESOURCE_BARRIER::Transition(
				GetTextureResource(T.TexData.ID),
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_COPY_DEST,
				NextMip
			);

			TicketGPU MappingDone = MapVirtualTextureMip(T, NextMip);

			EnqueueDelayedWork([TextureItem, NextMip, Index = i] {
				VirtualTexture& T = gScene.Textures[Index];

				TicketGPU UploadDone = UpdateVirtualTextureDirectStorage(
					T,
					NextMip,
					-TextureItem->UncompressedDataSize,
					gScene.FileReader.FileDS,
					TextureItem->DataOffset,
					TextureItem->CompressedDataSize,
					GetFileName(gScene.FileReader, *TextureItem).data()
				);

				EnqueueDelayedWork([Index, NextMip] {
					VirtualTexture& T = gScene.Textures[Index];
					T.NumStreamedIn++;
					T.StreamingInProgress = 0;

					auto CL = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Texture barriers");

					D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(T.TexData.ID),
						D3D12_RESOURCE_STATE_COPY_DEST,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						NextMip
					);

					CL->ResourceBarrier(1, &Barrier);
					Submit(CL);
				}, UploadDone);
			}, MappingDone);
			break;
		}
		else if (T.NumStreamedIn > 0 && DesiredMip > T.NumStreamedMips - T.NumStreamedIn)
		{
			i32 MipToUnload = (i32)T.NumStreamedMips - (i32)T.NumStreamedIn;
			T.NumStreamedIn--;
			CHECK(T.StreamedTileIds[MipToUnload] != 0);
			
			UnmapVirtualTextureMip(T, MipToUnload);
			break;
		}
	}
	if (!Barriers.empty())
	{
		auto CL = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Texture barriers");
		CL->ResourceBarrier(Barriers.size(), Barriers.data());
		Submit(CL);
	}
}

void StartSceneLoading(String FilePath)
{
	for (int i = 0; i < GENERAL_HEAP_SIZE; ++i)
	{
		gScene.DesiredMips[i] = 16384;
	}

	EnqueueToWorker([FilePath]() {
		TArray<TicketCPU> Tickets;

		Tickets.push_back() = EnqueueToWorkerWithTicket([FilePath]()
		{
			ZoneScopedN("SceneLoading");
			PakFileReader& SceneReader = gScene.FileReader = OpenPak(FilePath);

			const PakItem* CombinationsItem = FindItem(SceneReader, "___VertexCombinationsMask");

			auto Combinations = GetFileDataTyped<eastl::bitset<256>>(SceneReader, *CombinationsItem);
			//CHECK(Combinations.size() == 1);

			PakFileReader Shaders = OpenPak("./cooked/shaders.pak");
			const PakItem* SimpleShaderItem = FindItem(Shaders, "Simple");
			CHECK(SimpleShaderItem);
			String SimpleShaders = GetFileData(Shaders, *SimpleShaderItem);

			CHECK((SimpleShaderItem->PrivateFlags & (1 << 31)) == 0);
			u32 VSShaderSize = (SimpleShaderItem->PrivateFlags >> 16) & 0xffff;
			u32 PSShaderSize = SimpleShaderItem->PrivateFlags & 0xffff;
			ClosePak(Shaders);

			CHECK(SimpleShaders.size() == PSShaderSize + VSShaderSize);

			RawDataView VSShader((u8*)SimpleShaders.data(), VSShaderSize);
			RawDataView PSShader((u8*)SimpleShaders.data() + VSShaderSize, PSShaderSize);

			size_t SetBit = Combinations.find_first();
			while (SetBit != Combinations.size())
			{
				u32 RenderTargetFormat = SCENE_COLOR_FORMAT;
				Shader MeshShader = CreateShaderCombinationGraphics(
					(u8)SetBit,
					VSShader,
					PSShader,
					&RenderTargetFormat,
					DEPTH_FORMAT
				);

				EnqueueToRenderThread([SetBit, S = MOVE(MeshShader)]() mutable {
					gMeshShaders[SetBit] = MOVE(S);
				});
				
				SetBit = Combinations.find_next(SetBit);
			}

			const PakItem* NodesItem = FindItem(SceneReader, "___Scene_StaticGeometry");
			CHECK(NodesItem);

			EnqueueToRenderThread([Nodes = GetFileDataTypedArray<Node>(SceneReader, *NodesItem)]() mutable {
				gScene.StaticGeometry = MOVE(Nodes);
			});

			const PakItem* MaterialsItem = FindItem(SceneReader, "___Materials");
			CHECK(MaterialsItem);
			auto Materials = GetFileDataTypedArray<MaterialDescription>(SceneReader, *MaterialsItem);

			u64 NumTextures = 0;
			for (u64 i = 0; i < Materials.size(); ++i)
			{
				NumTextures = std::max((u64)Materials[i].DiffuseTexture, NumTextures);
			}
			TArray<VirtualTexture> Textures;

			Textures.reserve(NumTextures);
			TicketGPU Res {0};
			for (u64 i = 0; true ; ++i)
			{
				const PakItem* TextureItem = FindItem(SceneReader, StringFromFormat("___Texture_%d", i));
				if (TextureItem == nullptr)
				{
					break;
				}

				TextureDescription Desc{TextureItem->PrivateFlags};

				u32 Index = Textures.size();
				VirtualTexture& VTex = Textures.push_back();
				TextureData& Tex = VTex.TexData;
				Tex.Width   = GetTextureSize(Desc);
				Tex.Height  = GetTextureSize(Desc);
				Tex.Format  = GetTextureFormat(Desc);
				Tex.NumMips = GetMipCount(Desc);
				VTex.StreamingInProgress = 1;

				TicketGPU MemoryAlreadyMapped = CreateVirtualResourceForTexture(VTex, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST);
				CreateSRV(Tex, false);

				if (TextureItem->UncompressedDataSize < 0)
				{
					EnqueueDelayedWork([VTex, TextureItem]() mutable {
						TicketGPU UploadDone = UploadTextureDirectStorage(
							GetTextureResource(VTex.TexData.ID),
							VTex,
							-TextureItem->UncompressedDataSize,
							VTex.TexData.NumMips,
							gScene.FileReader.FileDS,
							TextureItem->DataOffset,
							TextureItem->CompressedDataSize,
							GetFileName(gScene.FileReader, *TextureItem).data()
						);

						EnqueueDelayedWork([ID = VTex.TexData.ID] {
							auto CL = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Texture barriers");

							D3D12_RESOURCE_BARRIER Barrier = CD3DX12_RESOURCE_BARRIER::Transition(
								GetTextureResource(ID),
								D3D12_RESOURCE_STATE_COPY_DEST,
								D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
								D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES
							);
							CL->ResourceBarrier(1, &Barrier);
							Submit(CL);
							StreamingStopped(ID);
						}, UploadDone);
					}, MemoryAlreadyMapped);
				}
				else if (TextureItem->UncompressedDataSize > 0)
				{
					String TexData(TextureItem->UncompressedDataSize, '\0');
					FillBuffer(SceneReader, *TextureItem, TexData.data());
					UploadTextureData(Tex, (u8*)TexData.data(), TextureItem->UncompressedDataSize);
				}
				else
				{
					UploadTextureData(Tex, (u8*)SceneReader.Mapping.BasePtr + TextureItem->DataOffset, TextureItem->UncompressedDataSize);
				}
			}

			gScene.Textures = MOVE(Textures);
			gScene.Materials = MOVE(Materials);

			const PakItem* CamerasItem = FindItem(SceneReader, "___Cameras");
			if (CamerasItem)
			{
				auto Cameras = GetFileDataTypedArray<Camera>(SceneReader, *CamerasItem);
				CHECK(Cameras.size() == 1);
				EnqueueToRenderThread([Camera = Cameras[0]]() mutable {
					MainCamera = Camera;
				});
			}

			const PakItem* VData = FindItem(SceneReader, "___Scene_Vertices");
			CHECK(VData);
			const PakItem* IData = FindItem(SceneReader, "___Scene_Indeces");
			CHECK(IData);

			TComPtr<ID3D12Resource> VResource;
			TComPtr<ID3D12Resource> IResource;
			TicketGPU DSDone = TicketGPU{};

			if (VData->UncompressedDataSize > 0)
			{
				CHECK(IData->UncompressedDataSize > 0);
				VResource = CreateBuffer(VData->UncompressedDataSize, BUFFER_GENERIC);
				IResource = CreateBuffer(IData->UncompressedDataSize, BUFFER_GENERIC);
				UploadBufferData(VResource.Get(), VData->UncompressedDataSize, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
					[&](void* GPUAddress, u64) {
						FillBuffer(SceneReader, *VData, GPUAddress);
					}
				);
				UploadBufferData(IResource.Get(), IData->UncompressedDataSize, D3D12_RESOURCE_STATE_INDEX_BUFFER,
					[&](void* GPUAddress, u64) {
						FillBuffer(SceneReader, *IData, GPUAddress);
					}
				);
			}
			else
			{
				CHECK(IData->UncompressedDataSize < 0);
				VResource = CreateBuffer(-VData->UncompressedDataSize, BUFFER_GENERIC);
				IResource = CreateBuffer(-IData->UncompressedDataSize, BUFFER_GENERIC);
				UploadBufferDirectStorage(VResource.Get(), -VData->UncompressedDataSize, SceneReader.FileDS, VData->DataOffset, VData->CompressedDataSize);
				DSDone = UploadBufferDirectStorage(IResource.Get(), -IData->UncompressedDataSize, SceneReader.FileDS, IData->DataOffset, IData->CompressedDataSize);
			}
			EnqueueToRenderThread([V = MOVE(VResource), I = MOVE(IResource)]() mutable {
				gScene.VertexBuffer = MOVE(V);
				gScene.IndexBuffer = MOVE(I);
				FlushUpload();
			});

			const PakItem* MeshDatas = FindItem(SceneReader, "___Scene_MeshDatas");
			CHECK(MeshDatas);

			{
				const PakItem* BufferOffsetsItem = FindItem(SceneReader, "___Scene_BufferOffsets");
				CHECK(BufferOffsetsItem);
				auto BufferOffests = GetFileDataTypedArray<MeshBufferOffsets>(SceneReader, *BufferOffsetsItem);

				ZoneScopedN("Upload mesh data");
				auto Datas = GetFileDataTypedArray<MeshDescription>(SceneReader, *MeshDatas);

				TArray<APIMesh> Meshes;
				Meshes.reserve(Datas.size());
				for (u64 i = 0; i < Datas.size(); ++i)
				{
					APIMesh& Tmp = Meshes.push_back();
					Tmp.Description = Datas[i];
					MeshBufferOffsets Offsets = BufferOffests[i];
					Tmp.VertexBufferCachedPtr = Offsets.VBufferOffset;
					Tmp.IndexBufferCachedPtr  = Offsets.IBufferOffset;
				}
				EnqueueDelayedWork([SceneReader, M = MOVE(Meshes)]() mutable {
#if 0
					D3D12CmdList CmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"After DS List");
					CD3DX12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							gScene.VertexBuffer.Get(),
							D3D12_RESOURCE_STATE_COMMON,
							D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							gScene.IndexBuffer.Get(),
							D3D12_RESOURCE_STATE_COMMON,
							D3D12_RESOURCE_STATE_INDEX_BUFFER
						),
					};
					CmdList->ResourceBarrier(ArrayCount(barriers), barriers);
					Submit(CmdList);
#endif
					u64 NewSize = gScene.MeshDatas.size() + M.size();
					gScene.MeshDatas.resize(NewSize);
					auto VBAddress = gScene.VertexBuffer->GetGPUVirtualAddress();
					auto IBAddress = gScene.IndexBuffer->GetGPUVirtualAddress();
					for (int i = 0; i < M.size(); ++i)
					{
						M[i].VertexBufferCachedPtr += VBAddress;
						M[i].IndexBufferCachedPtr  += IBAddress;
						M[i].PSO = gMeshShaders[M[i].Description.Flags].PSO.Get();
						gScene.MeshDatas[i + NewSize - M.size()] = MOVE(M[i]);
					}
				}, DSDone);
			}
		});
		for (TicketCPU T : Tickets)
		{
			WaitForCompletion(T);
		}
	});
}

extern TracyD3D12Ctx	gCopyProfilingCtx;
int main(void)
{
	//while(!IsDebuggerPresent())
	//{
		//Sleep(100);
	//}

	SetThreadAffinityMask(GetCurrentThread(), 0x1);
	stbi_set_flip_vertically_on_load(false);

	System::Window Window;
	Window.Init();

	InitDirectStorage();
	InitRender(Window);
	StartRenderThread();

	StartWorkerThreads();

	gGraphicsProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT));
	gComputeProfilingCtx = TracyD3D12Context(GetGraphicsDevice(), GetGPUQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE));

	TracyD3D12ContextName(gGraphicsProfilingCtx, "Graphics", 8);

	InitGUI(Window);

#if 0
	String FilePath = "cooked/DamagedHelmet.glbpak";
#elif 1
	String FilePath = "cooked/Bistro/BistroExterior.fbxpak";
#else
	String FilePath = "cooked/EmeraldSquare/EmeraldSquare_Day.fbxpak";
#endif

	StartSceneLoading(FilePath);

	PakFileReader ShaderReader = OpenPak("./cooked/shaders.pak");
	const PakItem* SpdItem = FindItem(ShaderReader, "FfxSpd");
	String FfxSpdCode = GetFileData(ShaderReader, *SpdItem);

	Shader FfxSpd = CreateShaderCombinationCompute(FfxSpdCode);

	const PakItem* ClearBufferItem = FindItem(ShaderReader, "ClearBuffer");
	String ClearBufferCode = GetFileData(ShaderReader, *ClearBufferItem);
	Shader ClearBuffer = CreateShaderCombinationCompute(ClearBufferCode);

	Shader BlitShader;
	{
		const PakItem* ShaderItem = FindItem(ShaderReader, "Blit");

		String GUIShaders = GetFileData(ShaderReader, *ShaderItem);

		CHECK((ShaderItem->PrivateFlags & (1 << 31)) == 0);
		u32 VSShaderSize = (ShaderItem->PrivateFlags >> 16) & 0xffff;
		u32 PSShaderSize = ShaderItem->PrivateFlags & 0xffff;

		CHECK(GUIShaders.size() == PSShaderSize + VSShaderSize);

		RawDataView VSShader((u8*)GUIShaders.data(), VSShaderSize);
		RawDataView PSShader((u8*)GUIShaders.data() + VSShaderSize, PSShaderSize);

		u32 RenderTargetFormat = BACK_BUFFER_FORMAT;
		BlitShader = CreateShaderCombinationGraphics(
			MeshFlags::GUI,
			VSShader,
			PSShader,
			&RenderTargetFormat,
			DXGI_FORMAT_UNKNOWN
		);
	}

	Shader GuiShader;
	{
		const PakItem* ShaderItem = FindItem(ShaderReader, "GUI");

		String GUIShaders = GetFileData(ShaderReader, *ShaderItem);

		CHECK((ShaderItem->PrivateFlags & (1 << 31)) == 0);
		u32 VSShaderSize = (ShaderItem->PrivateFlags >> 16) & 0xffff;
		u32 PSShaderSize = ShaderItem->PrivateFlags & 0xffff;

		CHECK(GUIShaders.size() == PSShaderSize + VSShaderSize);

		RawDataView VSShader((u8*)GUIShaders.data(), VSShaderSize);
		RawDataView PSShader((u8*)GUIShaders.data() + VSShaderSize, PSShaderSize);

		u32 RenderTargetFormat = BACK_BUFFER_FORMAT;
		GuiShader = CreateShaderCombinationGraphics(
			MeshFlags::GUI,
			VSShader,
			PSShader,
			&RenderTargetFormat,
			DXGI_FORMAT_UNKNOWN
		);
	}
	ClosePak(ShaderReader);

	TextureData DefaultTexture;
	{
		int w = 0, h = 0;
		uint8_t *Data = stbi_load("content/uvcheck.jpg", &w, &h, nullptr, 4);
		DefaultTexture.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		DefaultTexture.Width  = (u16)w;
		DefaultTexture.Height = (u16)h;
		DefaultTexture.NumMips = 1;

		CreateResourceForTexture(DefaultTexture, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COMMON, nullptr);
		UploadTextureData(DefaultTexture, Data, 0);
		CreateSRV(DefaultTexture, false);
		stbi_image_free(Data);
	}

	TextureData SceneColor = {};
	{
		SceneColor.Format = SCENE_COLOR_FORMAT;
		SceneColor.Width  = (u16)Window.mSize.x;
		SceneColor.Height = (u16)Window.mSize.y;
		SceneColor.NumMips = 1;

		GetTransientTexture(SceneColor,
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			nullptr
		);
	}

	TextureData DepthBuffer;
	auto CreateDepth = [&]()
	{
		D3D12_CLEAR_VALUE ClearValue = {};
		ClearValue.Format = DEPTH_FORMAT;
		ClearValue.DepthStencil.Depth = 0.0f;

		DepthBuffer.Format = (u8)DEPTH_FORMAT;
		DepthBuffer.Width = (u16)Window.mSize.x;
		DepthBuffer.Height = (u16)Window.mSize.y;

		CreateResourceForTexture(DepthBuffer, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE, &ClearValue);
		CreateDSV(DepthBuffer);
	};
	CreateDepth();

	UINT CurrentBackBufferIndex = 0;


	struct RenderGraph
	{
		enum NodeType
		{
			Output,
			GeometryPass,
			FullscreenPass,
		};
		struct NodeResult
		{
			TComPtr<ID3D12Resource> Resource;
			D3D12_RESOURCE_STATES ResourceState;
		};
		struct Node
		{
			TFunction<void (struct RenderGraph&, struct Node&)> Work;
			TArray<uint16_t> Inputs;
			TArray<uint16_t> Outputs;
			NodeType Type = NodeType::Output;
		};
		struct Edge
		{
			int ID;
			int StartID;
			int EndID;
		};

		TArray<Node> Nodes;
		TArray<Edge> Edges;
		TArray<bool> ValidNodes;
	};

	RenderGraph Graph;

	auto AddNode = [&Graph]() -> RenderGraph::Node& {
		for (int i = 0; i < Graph.ValidNodes.size(); ++i)
		{
			if (!Graph.ValidNodes[i])
			{
				Graph.ValidNodes[i] = true;
				return Graph.Nodes[i];
			}
		}

		Graph.ValidNodes.push_back(true);
		return Graph.Nodes.push_back();
	};

	auto RemoveNode = [&Graph](int NodeID) {
		Graph.ValidNodes[NodeID] = false;
		Graph.Edges.erase(
			eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
			[NodeID](RenderGraph::Edge& Edge) {
				return Edge.StartID == NodeID || Edge.EndID == NodeID;
			}),
			Graph.Edges.end()
		);
	};

	static TComPtr<ID3D12Resource> AtomicBuffer = CreateBuffer(6 * 4, BUFFER_GENERIC);

	static TextureData OutputTextures[12];
	for (int i = 0; i < 12; ++i)
	{
		OutputTextures[i].Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
		OutputTextures[i].Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	}
	
	u16 StartIndex = CreateUAVBatched(OutputTextures, ArrayCount(OutputTextures));
	static u64 OutputUAVDescriptors = GetGeneralHandleGPU(StartIndex);

	{
		gScene.WantedMips = CreateBuffer(GENERAL_HEAP_SIZE * 4, BUFFER_GENERIC);
		gScene.PickingBuffer = CreateBuffer(4, BUFFER_GENERIC);
		D3D12CmdList CmdList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Pre first frame");

		CD3DX12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				gScene.WantedMips,
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_COPY_SOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				gScene.PickingBuffer,
				D3D12_RESOURCE_STATE_COMMON,
				D3D12_RESOURCE_STATE_COPY_SOURCE
			),
		};
		CmdList->ResourceBarrier(ArrayCount(barriers), barriers);
		Submit(CmdList);
	}

	double Time = glfwGetTime();
	float DeltaTime = 0.1;
	while (!glfwWindowShouldClose(Window.mHandle))
	{
		FrameMark;

		if (Window.mWindowStateDirty)
		{
			ZoneScopedN("Window state dirty");

			// Wait for render thread AND gpu
			TicketCPU WaitForAll = EnqueueToRenderThreadWithTicket([]() {
				ZoneScopedN("Wait for render thread AND gpu");
				FlushQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
			});

			WaitForCompletion(WaitForAll);
			CreateBackBufferResources(Window);
			Window.mWindowStateDirty = false;
			CurrentBackBufferIndex = 0;

			if (Window.mSize.x != 0 && Window.mSize.y != 0)
			{
				DiscardTransientTexture(SceneColor);
				SceneColor.Width  = (u16)Window.mSize.x;
				SceneColor.Height = (u16)Window.mSize.y;
				GetTransientTexture(SceneColor,
					D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					nullptr
				);

				CreateDepth();
			}
		}

		if (Window.mSize.x == 0)
		{
			Window.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			continue;
		}

		UpdateGUI(Window);

		ImGui::NewFrame();

		static bool bRenderGraphOpen = false;

		//ImGui::SetNextWindowSize(ImVec2(500, 500));
		if (ImGui::Begin("RenderGraph", &bRenderGraphOpen))
		{
			ZoneScopedN("RenderGraphEditor");

			int StartNodeID, EndNodeID, StartAttribID, EndAttribID;
			bool FromSnap;
			if (ImNodes::IsLinkCreated(&StartNodeID, &StartAttribID, &EndNodeID, &EndAttribID, &FromSnap))
			{
				static int UID;
				RenderGraph::Edge& Edge = Graph.Edges.push_back();
				Edge.ID = UID++;
				Edge.StartID = StartAttribID;
				Edge.EndID = EndAttribID;
			}

			{
				int LinkID;
				if (ImNodes::IsLinkHovered(&LinkID) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					Graph.Edges.erase(
						eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
							[LinkID](RenderGraph::Edge& Edge) {return Edge.ID == LinkID; }),
						Graph.Edges.end()
					);
				}
			}

			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
			{
				if (int NodesNum = ImNodes::NumSelectedNodes())
				{
					TArray<int> Nodes;
					Nodes.resize(NodesNum);
					ImNodes::GetSelectedNodes(Nodes.data());
					for (int i : Nodes)
					{
						RemoveNode(i);
					}
				}
				if (int LinksNum = ImNodes::NumSelectedLinks())
				{
					TArray<int> Links;
					Links.resize(LinksNum);
					ImNodes::GetSelectedNodes(Links.data());

					Graph.Edges.erase(
						eastl::remove_if(Graph.Edges.begin(), Graph.Edges.end(),
							[&Links](RenderGraph::Edge& Edge) {
								return std::find(Links.begin(),Links.end(), Edge.ID) != Links.end();
							}),
						Graph.Edges.end()
					);
				}
			}

			ImNodes::BeginNodeEditor();

			if (ImNodes::IsEditorHovered()
			&& ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right))
			{
				ImGui::OpenPopup("RightClickNodePopup");
			}
			if (ImGui::BeginPopup("RightClickNodePopup"))
			{
				static unsigned short AttribID;
				size_t ID = Graph.Nodes.size();
				if (ImGui::MenuItem("Output"))
				{
					RenderGraph::Node& Node = AddNode();
					Node.Type = RenderGraph::Output;
					Node.Inputs.push_back(AttribID++);
					ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
				}
				if (ImGui::MenuItem("GeometryPass"))
				{
					RenderGraph::Node& Node = AddNode();
					Node.Type = RenderGraph::GeometryPass;
					Node.Inputs.push_back(AttribID++);
					Node.Outputs.push_back(AttribID++);
					ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
				}
				if (ImGui::MenuItem("FullscreenPass"))
				{
					RenderGraph::Node& Node = Graph.Nodes.push_back();
					Node.Type = RenderGraph::FullscreenPass;
					Node.Inputs.push_back(AttribID++);
					Node.Outputs.push_back(AttribID++);
					ImNodes::SetNodeScreenSpacePos((int)ID, ImGui::GetMousePos());
				}
				ImGui::EndPopup();
			}

			for (int i = 0; i < Graph.Nodes.size(); ++i)
			{
				RenderGraph::Node& Node = Graph.Nodes[i];
				ImNodes::BeginNode(i);

				if (Node.Type == RenderGraph::Output)
				{
					ImNodes::BeginNodeTitleBar();
					ImGui::Text("Output (backbuffer)");
					ImNodes::EndNodeTitleBar();

					for (int In : Node.Inputs)
					{
						ImNodes::BeginInputAttribute(In);
						ImGui::TextUnformatted("Backbuffer");
						ImNodes::EndInputAttribute();
					}
				}
				else if (Node.Type == RenderGraph::GeometryPass)
				{
					ImNodes::BeginNodeTitleBar();
					ImGui::Text("Geometry pass");
					ImNodes::EndNodeTitleBar();

					static int current = 0;
					static const char* items[] = {
						"Forward",
						"GBuffer",
					};

					ImGui::PushItemWidth(100);
					ImGui::Combo("Shader:", &current, items, ArrayCount(items));
					ImGui::PopItemWidth();

					for (int In : Node.Inputs)
					{
						ImNodes::BeginInputAttribute(In);
						ImGui::TextUnformatted("");
						ImNodes::EndInputAttribute();
					}

					for (int Out : Node.Outputs)
					{
						ImNodes::BeginOutputAttribute(Out);
						ImGui::TextUnformatted("");
						ImNodes::EndOutputAttribute();
					}
				}
				else if (Node.Type == RenderGraph::FullscreenPass)
				{
					ImNodes::BeginNodeTitleBar();
					ImGui::Text("Fullscreen pass");
					ImNodes::EndNodeTitleBar();

					for (int In : Node.Inputs)
					{
						ImNodes::BeginInputAttribute(In);
						ImGui::TextUnformatted("");
						ImNodes::EndInputAttribute();
					}

					static int current = 0;
					static const char* items[] = {
						"Downsample",
						"",
					};

					ImGui::PushItemWidth(100);
					ImGui::Combo("Shader:", &current, items, ArrayCount(items));
					ImGui::PopItemWidth();

					for (int Out : Node.Outputs)
					{
						ImNodes::BeginOutputAttribute(Out);
						ImGui::TextUnformatted("");
						ImNodes::EndOutputAttribute();
					}
				}
				ImNodes::EndNode();
			}

			for (int i = 0; i < Graph.Edges.size(); ++i)
			{
				RenderGraph::Edge& Edge = Graph.Edges[i];

				//ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, 1.f);
				ImNodes::Link(Edge.ID, Edge.StartID, Edge.EndID);
				//ImNodes::PopStyleVar();
			}

			ImNodes::EndNodeEditor();
			ImGui::End();
		}

		{
			ZoneScopedN("Wait for free backbuffer in swapchain");
			ExecuteMainThreadWork();

			//while (FenceWithDelay - LastCompletedFence > 2)
			while (!IsSwapChainReady())
			{
				//if (!StealWork())
				{
					ExecuteMainThreadWork();
					StealWork();
					//std::this_thread::sleep_for(std::chrono::microseconds(10));
				}
			}
			Window.Update();
		}

		EnqueueToRenderThread(
			[
				&SceneColor,
				&DepthBuffer
			]() {
				FrameMarkStart("Render thread");
				PIXBeginEvent(GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT), __LINE__, "FRAME");

				TracyD3D12NewFrame(gGraphicsProfilingCtx);
				ZoneScopedN("New frame");

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Clear");
				CD3DX12_RESOURCE_BARRIER barriers[] = {
					// SceneColor(srv -> render)
					CD3DX12_RESOURCE_BARRIER::Transition(
						GetTextureResource(SceneColor.ID),
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_RENDER_TARGET
					),
					CD3DX12_RESOURCE_BARRIER::Transition(
						gScene.WantedMips,
						D3D12_RESOURCE_STATE_COPY_SOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					),
					CD3DX12_RESOURCE_BARRIER::Transition(
						gScene.PickingBuffer,
						D3D12_RESOURCE_STATE_COPY_SOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					),
				};
				CommandList->ResourceBarrier(ArrayCount(barriers), barriers);

				CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
				CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

				CommandList->RSSetViewports(1, &SceneColorViewport);
				CommandList->RSSetScissorRects(1, &SceneColorScissor);

				BindRenderTargets(CommandList.Get(), {SceneColor.RTV}, DepthBuffer.DSV);
				{
					FLOAT clearColor[] = { 0.4f, 0.6f, 0.9f, 1.0f };
					ClearRenderTarget(CommandList.Get(), SceneColor.RTV, clearColor);
				}
				ClearDepth(CommandList.Get(), DepthBuffer.DSV, 0.0f);

				Submit(CommandList);
			}
		);

		{
			ZoneScopedN("ImGui sliders");
			static float CamSpeed = 1.0f;

			//if (!gScenes.empty())
			{
				auto& Scene = gScene;
				ImGui::DragFloat3("Pos", &MainCamera.Position.x);
				ImGui::SliderAngle("X", &MainCamera.Angles.x);
				ImGui::SliderAngle("Y", &MainCamera.Angles.y);
				ImGui::DragFloat("Camera speed", &CamSpeed);

				if (Window.mMouseButtons[GLFW_MOUSE_BUTTON_2])
				{
					glfwSetInputMode(Window.mHandle, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
					MainCamera.Angles.x += Window.mMouseOffset.x * .01f;
					MainCamera.Angles.y += Window.mMouseOffset.y * .01f;
				}
				else
				{
					glfwSetInputMode(Window.mHandle, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
				}
			}
			CamSpeed += Window.mScrollOffset.y;

			float SpeedThisFrame = CamSpeed * DeltaTime;
			if (Window.mMods & GLFW_MOD_SHIFT)
			{
				SpeedThisFrame *= 2.f;
			}
			if (Window.mKeyboard[GLFW_KEY_W])
			{
				MainCamera.Position += Vec4{0, 0, SpeedThisFrame, 0} * CreateRotationMatrix(MainCamera.Angles);
			}
			else if (Window.mKeyboard[GLFW_KEY_S])
			{
				MainCamera.Position += Vec4{0, 0, -SpeedThisFrame, 0} * CreateRotationMatrix(MainCamera.Angles);
			}
			if (Window.mKeyboard[GLFW_KEY_A])
			{
				MainCamera.Position += Vec4{-SpeedThisFrame, 0, 0, 0} * CreateRotationMatrix(MainCamera.Angles);
			}
			else if (Window.mKeyboard[GLFW_KEY_D])
			{
				MainCamera.Position += Vec4{SpeedThisFrame, 0, 0, 0} * CreateRotationMatrix(MainCamera.Angles);
			}
			if (Window.mKeyboard[GLFW_KEY_Q])
			{
				MainCamera.Position += Vec4{0, -SpeedThisFrame, 0, 0};
			}
			else if (Window.mKeyboard[GLFW_KEY_E])
			{
				MainCamera.Position += Vec4{0, SpeedThisFrame, 0, 0};
			}

			TicketCPU Culling = EnqueueToWorkerWithTicket([Camera = MainCamera]() {
				for (auto& k : gScene.StaticGeometry)
				{
					Vec3 Pos = k.Transform.Row(3);
					float DistSquared = Dot(Camera.Position, Pos);
					if (DistSquared > k.Bounds.SphereRadius)
					{
						float kl = k.Bounds.SphereRadius;
						kl += 0.1f;
					}
				}
			});

			EnqueueToRenderThread(
				[
					MouseX = u32(Window.mMousePosition.x),
					MouseY = u32(Window.mMousePosition.y),
					&DefaultTexture,
					&SceneColor,
					&DepthBuffer,
					&Window,
					&ClearBuffer,
					DeltaTime
				]()
				{
					if (gScene.StaticGeometry.empty())
						return;

					auto& Scene = gScene;

					TArray<D3D12CmdList> CommandLists;
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Main thread drawing meshes");
					{
						CommandList->SetComputeRootSignature(ClearBuffer.RootSignature);
						CommandList->SetPipelineState(ClearBuffer.PSO.Get());

						CommandList->SetComputeRootUnorderedAccessView(1, Scene.WantedMips->GetGPUVirtualAddress());

						struct {
							u32 Value;
							u32 Size;
						} Data;
						Data.Size = GENERAL_HEAP_SIZE;
						Data.Value = 16 * 1024;

						//CommandList->SetComputeRootDescriptorTable(2);
						//PooledBuffer AtomicBuffer;
						//GetTransientBuffer(AtomicBuffer, 6 * 4, BUFFER_GENERIC);
						//CommandList->ClearUnorderedAccessViewFloat();
						CommandList->SetComputeRoot32BitConstants(3, sizeof(Data) / 4, &Data, 0);

						u32 GroupCount = GENERAL_HEAP_SIZE / 256;
						CommandList->Dispatch(GroupCount, 1, 1);

						CommandList->SetComputeRootUnorderedAccessView(1, Scene.PickingBuffer->GetGPUVirtualAddress());

						Data.Size = 1;
						Data.Value = 0;
						CommandList->SetComputeRoot32BitConstants(3, sizeof(Data) / 4, &Data, 0);
						CommandList->Dispatch(1, 1, 1);

						Data.Size = 1;
						Data.Value = UINT32_MAX;

						{
							CD3DX12_RESOURCE_BARRIER barriers[] = {CD3DX12_RESOURCE_BARRIER::UAV(nullptr)};
							CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
						}
					}
					{
						ZoneScopedN("Drawing meshes");

						const int DrawingThreads = 2;

						//Camera MainCamera = MainCamera;
						float Fov = 1;// MainCamera.Fov;
						float Near = 1;// MainCamera.Near;

						Matrix4 Projection = CreatePerspectiveMatrixReverseZ(Fov, (float)Window.mSize.x / (float)Window.mSize.y, Near);
						//Matrix4 Projection = CreatePerspectiveMatrixClassic(Fov, (float)Window.mSize.x / (float)Window.mSize.y, Near, 100000);
						//Matrix4 View = InverseAffine(Scene.StaticGeometry[MainCamera.NodeID].Transform);
						Matrix4 View = CreateViewMatrix(MainCamera.Position, -MainCamera.Angles);
						Matrix4 VP = View * Projection;

						CommandLists.push_back(MOVE(CommandList));
						CommandLists.resize(DrawingThreads);
						for (auto It = CommandLists.begin() + 1; It != CommandLists.end(); ++It)
						{
							*It = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Worker thread drawing meshes");
						}

						ParallelFor(
							[
								MouseX, MouseY,
								&Scene,
								&CommandLists,
								&DefaultTexture,
								&VP, &SceneColor, &DepthBuffer,
								&ClearBuffer,
								DeltaTime
							](u64 Index, u64 Begin, u64 End)
							{
								D3D12CmdList& CmdList = CommandLists[Index];
								ID3D12GraphicsCommandList7* CommandList = CmdList.Get();

								TracyD3D12Zone(gGraphicsProfilingCtx, CommandList, "Render Meshes from worker");
								PIXScopedEvent(CommandList, __LINE__, "Render Meshes from worker");

								CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)SceneColor.Width, (float)SceneColor.Height);
								CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

								CommandList->RSSetViewports(1, &SceneColorViewport);
								CommandList->RSSetScissorRects(1, &SceneColorScissor);
							#if 0
								if (gDeviceCaps.VariableShadingRateTier == 2)
								{
									CommandList->RSSetShadingRate(D3D12_SHADING_RATE_4X4, nullptr);
								}
								else if (gDeviceCaps.VariableShadingRateTier == 1)
								{
									CommandList->RSSetShadingRate(D3D12_SHADING_RATE_2X2, nullptr);
								}
							#endif

								BindRenderTargets(CommandList, { SceneColor.RTV }, DepthBuffer.DSV);
								CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

								BindDescriptors(CommandList, 0);
								CommandList->SetGraphicsRootUnorderedAccessView(1, Scene.WantedMips->GetGPUVirtualAddress());
								CommandList->SetGraphicsRootUnorderedAccessView(2, Scene.PickingBuffer->GetGPUVirtualAddress());

								ID3D12PipelineState* CurrentPSO = nullptr;

								for (u64 i = Begin; i < End; ++i)
								{
									auto& Mesh = Scene.StaticGeometry[i];
									u32 MeshCount = (u32)Mesh.MeshCount;
									for (u32 ID = Mesh.MeshIDStart; ID < Mesh.MeshIDStart + MeshCount; ID++)
									{
										if (ID >= Scene.MeshDatas.size())
											continue;

										auto& MeshData = Scene.MeshDatas[ID];
										auto& Desc = MeshData.Description;
										if (gMeshShaders.find(Desc.Flags) == gMeshShaders.end())
											continue;

										Matrix4 Combined = Mesh.Transform * VP;
										if (Desc.Flags & MeshFlags::PositionPacked)
										{
											//Vec3 Scale = Desc.BoxMax - Desc.BoxMax;
											//Matrix4 ScaleM = CreateScaleMatrix(Scale);
											//Matrix4 TranslationM = CreateTranslationMatrix(Desc.BoxMin);
											//Combined = ScaleM * TranslationM * Combined;
										}

										u32 MatIndex = Desc.MaterialIndex;
										u32 TexIndex = Scene.Materials[MatIndex].DiffuseTexture;
										u32 SRV = DefaultTexture.SRV;
										float LodClampIndex = 0;
										if (TexIndex < Scene.Textures.size())
										{
											auto& T = Scene.Textures[TexIndex];
											SRV = T.TexData.SRV;
											LodClampIndex = (float)T.NumStreamedMips - T.NumStreamedIn;
										}

										CommandList->SetGraphicsRoot32BitConstants(3, sizeof(Combined) / 4, &Combined, 0);
										CommandList->SetGraphicsRoot32BitConstants(3, 1, &SRV, 16);
										CommandList->SetGraphicsRoot32BitConstants(3, 1, &LodClampIndex, 17);

										CommandList->SetGraphicsRoot32BitConstants(3, 1, &MouseX, 18);
										CommandList->SetGraphicsRoot32BitConstants(3, 1, &MouseY, 19);

										if (MeshData.PSO != CurrentPSO)
										{
											CommandList->SetPipelineState(MeshData.PSO);
											CurrentPSO = MeshData.PSO;
										}

										D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
										VertexBufferView.BufferLocation = MeshData.VertexBufferCachedPtr;
										VertexBufferView.SizeInBytes = GetVertexBufferSize(Desc);
										VertexBufferView.StrideInBytes = Desc.VertexSize;
										CommandList->IASetVertexBuffers(0, 1, &VertexBufferView);

										D3D12_INDEX_BUFFER_VIEW IndexBufferView;
										IndexBufferView.BufferLocation = MeshData.IndexBufferCachedPtr;
										IndexBufferView.SizeInBytes = GetIndexBufferSize(Desc);

										if (Desc.VertexCount <= UINT16_MAX)
											IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
										else
											IndexBufferView.Format = DXGI_FORMAT_R32_UINT;

										CommandList->IASetIndexBuffer(&IndexBufferView);

										CommandList->DrawIndexedInstanced(Desc.IndexCount, 1, 0, 0, 0);
									}
								}
							}, Scene.StaticGeometry.size(), DrawingThreads
						);
					}
					Submit(CommandLists);
				}
			);
		}

		EnqueueToRenderThread(
			[
				&SceneColor,
				&BlitShader,
				&Window,
				CurrentBackBufferIndex
			]
			() {
				ZoneScopedN("SceneColor(render -> srv)");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Blit scenecolor to backbuffer");
				{
					ZoneScopedN("SceneColor barrier");
					CD3DX12_RESOURCE_BARRIER barriers[] =
					{
						// scenecolot(render -> srv)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetTextureResource(SceneColor.ID),
							D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),

						// backbuffer(present -> render)
						CD3DX12_RESOURCE_BARRIER::Transition(
							GetBackBufferResource(CurrentBackBufferIndex),
							D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							gScene.WantedMips,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_COPY_SOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							gScene.PickingBuffer,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_COPY_SOURCE
						),
					};

					CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
					{
						ZoneScopedN("epilogue");

						TracyD3D12Zone(gGraphicsProfilingCtx, CommandList, "Render Meshes from worker");
						PIXScopedEvent(CommandList, __LINE__, "Render Meshes from worker");

						PooledBuffer Feedback;
						GetTransientBuffer(Feedback, GENERAL_HEAP_SIZE * 4, BUFFER_READBACK);

						PooledBuffer Picking;
						GetTransientBuffer(Picking, 4, BUFFER_READBACK);

						CommandList->CopyBufferRegion(Picking.Resource, Picking.Offset, gScene.PickingBuffer, 0, 4);

						CommandList->CopyBufferRegion(Feedback.Resource, Feedback.Offset, gScene.WantedMips, 0, GENERAL_HEAP_SIZE * 4);

						TicketGPU FrameDone = CurrentFrameTicket();
						EnqueueDelayedWork([Feedback, Picking]() mutable {
							auto Mips = (u32*)Feedback.CPUPtr;
							for (int i = 0; i < GENERAL_HEAP_SIZE; ++i)
							{
								if (Mips[i] > gScene.DesiredMips[i])
								{
									gScene.DesiredMips[i] = Lerp(float(Mips[i]), float(gScene.DesiredMips[i]), 0.95f);
								}
								else
								{
									gScene.DesiredMips[i] = Lerp(float(Mips[i]), float(gScene.DesiredMips[i]), 0.001f);
								}
							}
							DiscardTransientBuffer(Feedback);
							TickStreaming();
						
							u32 DepthAndSRV = *(u32*)Picking.CPUPtr;
							u16 DepthNorm = (DepthAndSRV>>16) & 0xffff;
							float Depth = float(DepthNorm) / 65535.f;
							gScene.PickedSRV = u16(DepthAndSRV & 0xffff);
							DiscardTransientBuffer(Picking);
						}, FrameDone);
					}
				}
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Blit scenecolor to backbuffer");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Blit scenecolor to backbuffer");

					BindDescriptors(CommandList.Get(), SceneColor.SRV);

					CommandList->SetPipelineState(BlitShader.PSO.Get());

					BindRenderTargets(CommandList.Get(), {CurrentBackBufferIndex}, (uint32_t)-1);

					CD3DX12_VIEWPORT	Viewport    = CD3DX12_VIEWPORT(0.f, 0.f, (float)Window.mSize.x, (float)Window.mSize.y);
					CD3DX12_RECT		ScissorRect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
					CommandList->RSSetViewports(1, &Viewport);
					CommandList->RSSetScissorRects(1, &ScissorRect);

					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
					CommandList->DrawInstanced(3, 1, 0, 0);
				}
				Submit(CommandList);
			}
		);

		DrawDebugInfoDX12();
		DrawDebugInfoMain();
		{
			ZoneScopedN("ImGui endframe work");
			ImGui::Render();
		}

		ImDrawData *DrawData = ImGui::GetDrawData();
		if (DrawData && DrawData->TotalVtxCount != 0)
		{
			TArray<ImDrawList*> DrawLists;
			DrawLists.resize(DrawData->CmdListsCount);
			for (int i = 0; i < DrawData->CmdListsCount; ++i)
			{
				DrawLists[i] = DrawData->CmdLists[i]->CloneOutput();
			}

			EnqueueToRenderThread(
				[
					TotalVtxCount = DrawData->TotalVtxCount,
					TotalIdxCount = DrawData->TotalIdxCount,
					DrawLists = MOVE(DrawLists),
					&GuiShader,
					CurrentBackBufferIndex
				]()
			{
				ZoneScopedN("Render GUI");

				UINT VtxBufferSize = TotalVtxCount * sizeof(ImDrawVert);
				UINT IdxBufferSize = TotalIdxCount * sizeof(ImDrawIdx);

				PooledBuffer GuiVBuffer;
				GetTransientBuffer(GuiVBuffer, VtxBufferSize, BUFFER_UPLOAD);
				PooledBuffer GuiIBuffer;
				GetTransientBuffer(GuiIBuffer, IdxBufferSize, BUFFER_UPLOAD);

				{
					ZoneScopedN("GUI buffer upload");
					UINT64 VtxOffset = 0;
					UINT64 IdxOffset = 0;

					u8* VtxP = GuiVBuffer.CPUPtr;
					u8* IdxP = GuiIBuffer.CPUPtr;
					for (ImDrawList* ImGuiCmdList : DrawLists)
					{
						memcpy(VtxP + VtxOffset, ImGuiCmdList->VtxBuffer.Data, ImGuiCmdList->VtxBuffer.size_in_bytes());
						VtxOffset += ImGuiCmdList->VtxBuffer.Size * sizeof(ImDrawVert);
						memcpy(IdxP + IdxOffset, ImGuiCmdList->IdxBuffer.Data, ImGuiCmdList->IdxBuffer.size_in_bytes());
						IdxOffset += ImGuiCmdList->IdxBuffer.Size * sizeof(ImDrawIdx);
					}
				}

				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Rener GUI");
				{
					TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Render GUI");
					PIXScopedEvent(CommandList.Get(), __LINE__, "Render GUI");
					BindDescriptors(CommandList.Get(), 0);

					TextureData& RenderTarget = GetBackBuffer(CurrentBackBufferIndex);
					BindRenderTargets(CommandList.Get(), { RenderTarget.RTV }, (uint32_t)-1);
					CommandList->SetPipelineState(GuiShader.PSO.Get());
					CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

					Vec2 RenderTargetSize{ (float)RenderTarget.Width, (float)RenderTarget.Height };
					CommandList->SetGraphicsRoot32BitConstants(3, 2, &RenderTargetSize, 0);

					ImGuiTexIDWrapper TexID = ImGuiTexIDWrapper(GetGUIFont().SRV);
					CommandList->SetGraphicsRoot32BitConstants(3, 2, &TexID, 2);

					D3D12_INDEX_BUFFER_VIEW ImGuiIdxBufferView;
					ImGuiIdxBufferView.BufferLocation = GuiIBuffer->GetGPUVirtualAddress() + GuiIBuffer.Offset;
					ImGuiIdxBufferView.SizeInBytes    = IdxBufferSize;
					ImGuiIdxBufferView.Format         = DXGI_FORMAT_R16_UINT;
					CommandList->IASetIndexBuffer(&ImGuiIdxBufferView);

					D3D12_VERTEX_BUFFER_VIEW ImGuiVtxBufferView;
					ImGuiVtxBufferView.BufferLocation = GuiVBuffer->GetGPUVirtualAddress() + GuiVBuffer.Offset;
					ImGuiVtxBufferView.StrideInBytes  = sizeof(ImDrawVert);
					ImGuiVtxBufferView.SizeInBytes    = VtxBufferSize;
					CommandList->IASetVertexBuffers(0, 1, &ImGuiVtxBufferView);

					TextureData& BackBuffer = GetBackBuffer(CurrentBackBufferIndex);
					CD3DX12_VIEWPORT	SceneColorViewport = CD3DX12_VIEWPORT(0.f, 0.f, (float)BackBuffer.Width, (float)BackBuffer.Height);
					CD3DX12_RECT		SceneColorScissor = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);

					CommandList->RSSetViewports(1, &SceneColorViewport);
					CommandList->RSSetScissorRects(1, &SceneColorScissor);

					int VtxOffset = 0;
					int IdxOffset = 0;
					for (int i = 0; i < DrawLists.size(); ++i)
					{
						ImDrawList* ImGuiCmdList = DrawLists[i];

						for (auto& ImGuiCmd : ImGuiCmdList->CmdBuffer)
						{
							ImGuiTexIDWrapper NewTexID = ImGuiTexIDWrapper(ImGuiCmd.GetTexID());
							if (NewTexID != TexID)
							{
								TexID = NewTexID;
								CommandList->SetGraphicsRoot32BitConstants(3, 2, &TexID, 2);
							}
							D3D12_RECT Rect{
								LONG(ImGuiCmd.ClipRect.x),
								LONG(ImGuiCmd.ClipRect.y),
								LONG(ImGuiCmd.ClipRect.z),
								LONG(ImGuiCmd.ClipRect.w),
							};
							CommandList->RSSetScissorRects(1, &Rect);
							CommandList->DrawIndexedInstanced(ImGuiCmd.ElemCount, 1, IdxOffset + ImGuiCmd.IdxOffset, VtxOffset + ImGuiCmd.VtxOffset, 0);
						}
						VtxOffset += ImGuiCmdList->VtxBuffer.Size;
						IdxOffset += ImGuiCmdList->IdxBuffer.Size;
					}
				}
				Submit(CommandList);
				TicketGPU GuiRenderingDone = CurrentFrameTicket();

				EnqueueDelayedWork(
					[GuiVBuffer, GuiIBuffer]() mutable {
						DiscardTransientBuffer(GuiVBuffer);
						DiscardTransientBuffer(GuiIBuffer);
					},
					GuiRenderingDone
				);

				for (ImDrawList* List : DrawLists)
				{
					IM_DELETE(List);
				}

				FlushUpload();
				RunDelayedWork();
				//TickStreaming();
			});
		}

#define CHECK_SCREENSHOT_CODE 0
#if CHECK_SCREENSHOT_CODE //|| TRACY_ENABLE // Send screenshot to Tracy
		if (CHECK_SCREENSHOT_CODE || TracyIsConnected)
		{
			static bool ScreenShotInFlight = false;
			if (!ScreenShotInFlight)
			{
				ScreenShotInFlight = true;
				EnqueueToRenderThread(
					[
						&Window,
						&SceneColor,
						&FfxSpd
					] () {
					D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Downsample scenecolor to readback");

					TextureData Small = {};
					PooledBuffer SceneColorStagingBuffer;
					u16 x, y;
					u32 NeededMipIndex = -1;
					{
						ZoneScopedN("Downsample scenecolor to readback");
						TracyD3D12Zone(gGraphicsProfilingCtx, CommandList.Get(), "Downsample scenecolor to readback");
						PIXScopedEvent(CommandList.Get(), __LINE__, "Downsample scenecolor to readback");

						{
							Small.Format = READBACK_FORMAT;
							Small.Width  = (u16)Window.mSize.x;
							Small.Height = (u16)Window.mSize.y;
							while (Small.Width * Small.Height * 4 > 256kb)
							{
								Small.Width  >>= 1;
								Small.Height >>= 1;

								Small.Width = AlignUp<u16>(Small.Width, 4);
								Small.Height = AlignUp<u16>(Small.Height, 4);
								
								NeededMipIndex++;
							}
							GetTransientTexture(Small,
								D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
								D3D12_RESOURCE_STATE_COMMON,
								nullptr
							);
							D3D12_UNORDERED_ACCESS_VIEW_DESC Desc;
							Desc.Format = (DXGI_FORMAT)Small.Format;
							Desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
							Small.UAV = OutputTextures[NeededMipIndex].UAV;
							CreateUAV(Small);
						}
						x = Small.Width;
						y = Small.Height;

						CHECK(x % 4 == 0);
						CHECK(y % 4 == 0);

						{
							CD3DX12_RESOURCE_BARRIER barriers[] = {
								CD3DX12_RESOURCE_BARRIER::Transition(
									GetTextureResource(Small.ID),
									D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS
								),
								CD3DX12_RESOURCE_BARRIER::Transition(
									GetTextureResource(SceneColor.ID),
									D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
								),
							};
							CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
						}

						{
							CommandList->SetComputeRootSignature(FfxSpd.RootSignature);
							CommandList->SetPipelineState(FfxSpd.PSO.Get());
							BindDescriptors(CommandList.Get(), SceneColor.SRV);

							CommandList->SetComputeRootDescriptorTable(0, D3D12_GPU_DESCRIPTOR_HANDLE{GetGeneralHandleGPU(SceneColor.SRV)});
							//CommandList->SetComputeRootUnorderedAccessView();

							varAU2(dispatchThreadGroupCountXY); // output variable
							varAU2(workGroupOffset);  // output variable, this constants are required if Left and Top are not 0,0
							varAU2(numWorkGroupsAndMips); // output variable
							// input information about your source texture:
							// left and top of the rectancle within your texture you want to downsample
							// width and height of the rectancle you want to downsample
							// if complete source texture should get downsampled: left = 0, top = 0, width = sourceTexture.width, height = sourceTexture.height
							varAU4(rectInfo) = initAU4(0, 0, SceneColor.Width, SceneColor.Height); // left, top, width, height
							SpdSetup(dispatchThreadGroupCountXY, workGroupOffset, numWorkGroupsAndMips, rectInfo);
							// constants:
							struct SpdConstants {
							   u32 numWorkGroups;
							   u32 mips;
							   float invInputSizeX;
							   float invInputSizeY;
							};
							SpdConstants Data;
							Data.numWorkGroups = numWorkGroupsAndMips[0];
							Data.mips = numWorkGroupsAndMips[1];
							Data.invInputSizeX = 1.f / SceneColor.Width;
							Data.invInputSizeY = 1.f / SceneColor.Height;

							//CommandList->SetComputeRootDescriptorTable(2);
							//PooledBuffer AtomicBuffer;
							//GetTransientBuffer(AtomicBuffer, 6 * 4, BUFFER_GENERIC);
							//CommandList->ClearUnorderedAccessViewFloat();
							CommandList->SetComputeRootUnorderedAccessView(1, AtomicBuffer->GetGPUVirtualAddress());
							CommandList->SetComputeRootDescriptorTable(2, D3D12_GPU_DESCRIPTOR_HANDLE{OutputUAVDescriptors});
							CommandList->SetComputeRoot32BitConstants(3, 4, &Data, 0);

							uint32_t dispatchX = dispatchThreadGroupCountXY[0];
							uint32_t dispatchY = dispatchThreadGroupCountXY[1];
							uint32_t dispatchZ = 1;
							CommandList->Dispatch(dispatchX, dispatchY, dispatchZ);

							{
								CD3DX12_RESOURCE_BARRIER barriers[] = {
									CD3DX12_RESOURCE_BARRIER::Transition(
										GetTextureResource(Small.ID),
										D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON
									),
									CD3DX12_RESOURCE_BARRIER::Transition(
										GetTextureResource(SceneColor.ID),
										D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
									),
								};
								CommandList->ResourceBarrier(ArrayCount(barriers), barriers);
							}
						}
					}
					DiscardTransientTexture(Small);

					Submit(CommandList);

					TicketGPU DownsampleDone = CurrentFrameTicket();

					ZoneScopedN("Readback screen capture");
					TicketGPU CopyDone;
					{
						D3D12_RESOURCE_DESC Desc = GetTextureResource(Small.ID)->GetDesc();
						UINT RowCount;
						UINT64 RowPitch;
						UINT64 ResourceSize;
						GetGraphicsDevice()->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

						D3D12_PLACED_SUBRESOURCE_FOOTPRINT bufferFootprint = {};
						bufferFootprint.Footprint.Width = Small.Width;
						bufferFootprint.Footprint.Height = Small.Height;
						bufferFootprint.Footprint.Depth = 1;
						bufferFootprint.Footprint.RowPitch = (UINT)RowPitch;
						bufferFootprint.Footprint.Format = (DXGI_FORMAT)Small.Format;

						GetTransientBuffer(SceneColorStagingBuffer, ResourceSize, BUFFER_READBACK);

						CD3DX12_TEXTURE_COPY_LOCATION Dst(SceneColorStagingBuffer.Get(), bufferFootprint);
						CD3DX12_TEXTURE_COPY_LOCATION Src(GetTextureResource(Small.ID));

						InsertWait(D3D12_COMMAND_LIST_TYPE_COPY, DownsampleDone);
						D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_COPY, L"Copy screenshot texture");
						{
							//TracyD3D12Zone(gCopyProfilingCtx, CommandList.Get(), "Copy screenshot texture");
							CommandList->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
						}
						Submit(CommandList);
						CopyDone = Signal(D3D12_COMMAND_LIST_TYPE_COPY);
					}

					EnqueueDelayedWork(
						[
							SceneColorStagingBuffer, x, y
						]
					() mutable {
						auto ResourcePtr = SceneColorStagingBuffer.Get();

						UINT   RowCount;
						UINT64 RowPitch;
						UINT64 ResourceSize;
						D3D12_RESOURCE_DESC Desc = ResourcePtr->GetDesc();
						GetGraphicsDevice()->GetCopyableFootprints(&Desc, 0, 1, 0, NULL, &RowCount, &RowPitch, &ResourceSize);

						ZoneScopedN("Uploading screenshot to Tracy");
						D3D12_RANGE Range;
						Range.Begin = 0;
						Range.End = (UINT64)ResourceSize;
						void* Data = nullptr;
						VALIDATE(ResourcePtr->Map(0, &Range, &Data));
					#if TRACY_ENABLE
						FrameImage(Data, x, y, 3, false);
					#endif
						ResourcePtr->Unmap(0, NULL);

						DiscardTransientBuffer(SceneColorStagingBuffer);

						ScreenShotInFlight = false;
					},
					CopyDone);
				});
			}
		}
#endif
		EnqueueToRenderThread(
		[
			CurrentBackBufferIndex
		]()
		{
			{
				ZoneScopedN("Present");
				D3D12CmdList CommandList = GetCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, L"Present");
				{
					// backbuffer(render -> present)
					CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
						GetBackBufferResource(CurrentBackBufferIndex),
						D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
					CommandList->ResourceBarrier(1, &barrier);
				}
				Submit(CommandList);
				Signal(D3D12_COMMAND_LIST_TYPE_DIRECT);
				PresentCurrentBackBuffer();
			}

			TracyD3D12Collect(gGraphicsProfilingCtx);
			TracyD3D12Collect(gComputeProfilingCtx);

			PIXEndEvent(GetGPUQueue(D3D12_COMMAND_LIST_TYPE_DIRECT));

			FrameMarkEnd("Render thread");
		});

		CurrentBackBufferIndex = (CurrentBackBufferIndex + 1) % BACK_BUFFER_COUNT;

		DeltaTime = float(glfwGetTime() - Time);
		Time = glfwGetTime();
	}

	if (CurrentBackBufferIndex == 0)
	{
		CurrentBackBufferIndex = BACK_BUFFER_COUNT;
	}

	EnqueueToRenderThread([CurrentBackBufferIndex]() {
		FlushQueue(D3D12_COMMAND_LIST_TYPE_DIRECT);
		ReleaseTextures();
		TracyD3D12Destroy(gGraphicsProfilingCtx);
		TracyD3D12Destroy(gComputeProfilingCtx);
	});
	StopRenderThread();
	StopWorkerThreads();
}

