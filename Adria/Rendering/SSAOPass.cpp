#include "SSAOPass.h"
#include "Components.h"
#include "BlackboardData.h"
#include "PSOCache.h" 

#include "../RenderGraph/RenderGraph.h"
#include "../Utilities/Random.h"
#include "../Core/ConsoleVariable.h"
#include "../Editor/GUICommand.h"

using namespace DirectX;

namespace adria
{
	namespace cvars
	{
		static ConsoleVariable ssao_power("ssao.power",   4.0f);
		static ConsoleVariable ssao_radius("ssao.radius", 1.0f);
	}

	SSAOPass::SSAOPass(uint32 w, uint32 h) : width(w), height(h), ssao_random_texture(nullptr),
		blur_pass(w, h)
	{
		RealRandomGenerator rand_float{ 0.0f, 1.0f };
		for (uint32 i = 0; i < ARRAYSIZE(ssao_kernel); i++)
		{
			DirectX::XMFLOAT4 _offset = DirectX::XMFLOAT4(2 * rand_float() - 1, 2 * rand_float() - 1, rand_float(), 0.0f);
			DirectX::XMVECTOR offset = DirectX::XMLoadFloat4(&_offset);
			offset = DirectX::XMVector4Normalize(offset);
			offset *= rand_float();
			ssao_kernel[i] = offset;
		}
	}
	void SSAOPass::AddPass(RenderGraph& rendergraph)
	{
		struct SSAOPassData
		{
			RGTextureReadOnlyId gbuffer_normal_srv;
			RGTextureReadOnlyId depth_stencil_srv;
			RGTextureReadWriteId output_uav;
		};

		FrameBlackboardData const& global_data = rendergraph.GetBlackboard().GetChecked<FrameBlackboardData>();
		rendergraph.AddPass<SSAOPassData>("SSAO Pass",
			[=](SSAOPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc ssao_desc{};
				ssao_desc.format = GfxFormat::R8_UNORM;
				ssao_desc.width = width;
				ssao_desc.height = height;

				builder.DeclareTexture(RG_RES_NAME(SSAO_Output), ssao_desc);
				data.output_uav = builder.WriteTexture(RG_RES_NAME(SSAO_Output));
				data.gbuffer_normal_srv = builder.ReadTexture(RG_RES_NAME(GBufferNormal), ReadAccess_NonPixelShader);
				data.depth_stencil_srv = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
			},
			[&](SSAOPassData const& data, RenderGraphContext& ctx, GfxDevice* gfx, CommandList* cmd_list)
			{
				ID3D12Device* device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
				auto dynamic_allocator = gfx->GetDynamicAllocator();

				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::SSAO));

				uint32 i = (uint32)descriptor_allocator->AllocateRange(4);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), ctx.GetReadOnlyTexture(data.depth_stencil_srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadOnlyTexture(data.gbuffer_normal_srv), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 2), ssao_random_texture->GetSRV(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 3), ctx.GetReadWriteTexture(data.output_uav), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				struct SSAOConstants
				{
					float  radius;
					float  power;
					float  noise_scale_x;
					float  noise_scale_y;
		
					uint32   depth_idx;
					uint32   normal_idx;
					uint32   noise_idx;
					uint32   output_idx;
				} constants = 
				{
					.radius = params.ssao_radius, .power = params.ssao_power,
					.noise_scale_x = width * 1.0f / NOISE_DIM, .noise_scale_y = height * 1.0f / NOISE_DIM,
					.depth_idx = i, .normal_idx = i + 1, .noise_idx = i + 2, .output_idx = i + 3
				};
				DynamicAllocation alloc = dynamic_allocator->Allocate(sizeof(ssao_kernel), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
				alloc.Update(ssao_kernel, alloc.size);

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 8, &constants, 0);
				cmd_list->SetComputeRootConstantBufferView(2, alloc.gpu_address);
				cmd_list->Dispatch((UINT)std::ceil(width / 16.0f), (UINT)std::ceil(height / 16.0f), 1);
			}, RGPassType::Compute);

		blur_pass.AddPass(rendergraph, RG_RES_NAME(SSAO_Output), RG_RES_NAME(AmbientOcclusion), " SSAO");
		
		params.ssao_power = std::clamp(cvars::ssao_power.Get(), 1.0f, 16.0f);
		params.ssao_radius = std::clamp(cvars::ssao_radius.Get(), 0.5f, 4.0f);
		AddGUI([&]() 
			{
				if (ImGui::TreeNodeEx("SSAO", ImGuiTreeNodeFlags_OpenOnDoubleClick))
				{
					ImGui::SliderFloat("Power", &cvars::ssao_power.Get(), 1.0f, 16.0f);
					ImGui::SliderFloat("Radius", &cvars::ssao_radius.Get(), 0.5f, 4.0f);

					ImGui::TreePop();
					ImGui::Separator();
				}
			}
		);
	}

	void SSAOPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
		blur_pass.OnResize(w, h);
	}

	void SSAOPass::OnSceneInitialized(GfxDevice* gfx)
	{
		RealRandomGenerator rand_float{ 0.0f, 1.0f };
		std::vector<float> random_texture_data;
		for (int32 i = 0; i < 8 * 8; i++)
		{
			random_texture_data.push_back(rand_float()); 
			random_texture_data.push_back(rand_float());
			random_texture_data.push_back(0.0f);
			random_texture_data.push_back(1.0f);
		}

		GfxTextureInitialData data{};
		data.pData = random_texture_data.data();
		data.RowPitch = 8 * 4 * sizeof(float);
		data.SlicePitch = 0;

		GfxTextureDesc noise_desc{};
		noise_desc.width = NOISE_DIM;
		noise_desc.height = NOISE_DIM;
		noise_desc.format = GfxFormat::R32G32B32A32_FLOAT;
		noise_desc.initial_state = GfxResourceState::PixelShaderResource;
		noise_desc.bind_flags = GfxBindFlag::ShaderResource;

		ssao_random_texture = std::make_unique<GfxTexture>(gfx, noise_desc, &data);
		ssao_random_texture->CreateSRV();
		ssao_random_texture->GetNative()->SetName(L"SSAO Random Texture");
	}
}

