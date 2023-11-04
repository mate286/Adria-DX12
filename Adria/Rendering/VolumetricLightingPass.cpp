#include "VolumetricLightingPass.h"
#include "ShaderStructs.h"
#include "Components.h"
#include "BlackboardData.h"
#include "PSOCache.h" 

#include "Editor/GUICommand.h"
#include "Graphics/GfxRingDescriptorAllocator.h"
#include "RenderGraph/RenderGraph.h"
#include "Logging/Logger.h"


using namespace DirectX;

namespace adria
{

	void VolumetricLightingPass::AddPass(RenderGraph& rendergraph)
	{
		struct LightingPassData
		{
			RGTextureReadOnlyId  depth;
			RGTextureReadWriteId output;
		};

		FrameBlackboardData const& frame_data = rendergraph.GetBlackboard().Get<FrameBlackboardData>();
		rendergraph.AddPass<LightingPassData>("Volumetric Lighting Pass",
			[=](LightingPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc volumetric_output_desc{};
				volumetric_output_desc.width = width >> resolution;
				volumetric_output_desc.height = height >> resolution;
				volumetric_output_desc.format = GfxFormat::R16G16B16A16_FLOAT;
				builder.DeclareTexture(RG_RES_NAME(VolumetricLightOutput), volumetric_output_desc);

				data.output = builder.WriteTexture(RG_RES_NAME(VolumetricLightOutput));
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);

				for (auto& shadow_texture : shadow_textures) std::ignore = builder.ReadTexture(shadow_texture);
			},
			[=](LightingPassData const& data, RenderGraphContext& context, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();
				
				GfxDescriptor src_handles[] = { context.GetReadOnlyTexture(data.depth),
												context.GetReadWriteTexture(data.output) };
				GfxDescriptor dst_handle = gfx->AllocateDescriptorsGPU(ARRAYSIZE(src_handles));
				gfx->CopyDescriptors(dst_handle, src_handles);
				uint32 i = dst_handle.GetIndex();

				struct VolumetricLightingConstants
				{
					uint32 depth_idx;
					uint32 output_idx;
					uint32 resolution_scale;
				} constants =
				{
					.depth_idx = i, .output_idx = i + 1, .resolution_scale = (uint32)resolution
				};
				
				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::VolumetricLighting));
				cmd_list->SetRootCBV(0, frame_data.frame_cbuffer_address);
				cmd_list->SetRootConstants(1, constants);
				cmd_list->Dispatch((uint32)std::ceil((width >> resolution) / 16.0f), (uint32)std::ceil((height >> resolution) / 16.0f), 1);
			}, RGPassType::Compute);

		copy_to_texture_pass.AddPass(rendergraph, RG_RES_NAME(HDR_RenderTarget), RG_RES_NAME(VolumetricLightOutput), BlendMode::AdditiveBlend);

		GUI_RunCommand([&]()
			{
				if (ImGui::TreeNodeEx("Volumetric Lighting", ImGuiTreeNodeFlags_None))
				{
					static int _resolution = (int)resolution;
					if (ImGui::Combo("Volumetric Lighting Resolution", &_resolution, "Full\0Half\0Quarter\0", 3))
					{
						resolution = (VolumetricLightingResolution)_resolution;
						OnResize(width, height);
					}

					ImGui::TreePop();
					ImGui::Separator();
				}
			});
		shadow_textures.clear();
	}

}




