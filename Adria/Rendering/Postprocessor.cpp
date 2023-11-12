#include "PostProcessor.h"
#include "ShaderStructs.h"
#include "Components.h"
#include "BlackboardData.h"
#include "PSOCache.h"

#include "Graphics/GfxRingDescriptorAllocator.h"
#include "Graphics/GfxLinearDynamicAllocator.h"
#include "RenderGraph/RenderGraph.h"
#include "Logging/Logger.h"
#include "Core/ConsoleVariable.h"
#include "Editor/GUICommand.h"
#include "entt/entity/registry.hpp"

using namespace DirectX;

namespace adria
{
	namespace cvars
	{
		static ConsoleVariable upscaler("upscaler", 0);
		static ConsoleVariable reflections("reflections", 0);
		static ConsoleVariable taa("TAA", false);
		static ConsoleVariable fxaa("FXAA", true);
		static ConsoleVariable exposure("exposure", true);
		static ConsoleVariable clouds("clouds", true);
		static ConsoleVariable dof("dof", false);
		static ConsoleVariable bokeh("bokeh", false);
		static ConsoleVariable bloom("bloom", false);
		static ConsoleVariable motion_blur("motionblur", false);
		static ConsoleVariable fog("fog", false);
	}

	PostProcessor::PostProcessor(GfxDevice* gfx, entt::registry& reg, uint32 width, uint32 height)
		: gfx(gfx), reg(reg), display_width(width), display_height(height), render_width(width), render_height(height),
		blur_pass(width, height), copy_to_texture_pass(width, height),
		add_textures_pass(width, height), automatic_exposure_pass(width, height),
		lens_flare_pass(width, height), clouds_pass(width, height), ssr_pass(width, height), fog_pass(width, height),
		dof_pass(width, height), bloom_pass(width, height), velocity_buffer_pass(width, height),
		motion_blur_pass(width, height), taa_pass(width, height), god_rays_pass(width, height), bokeh_pass(width, height),
		fsr2_pass(gfx, width, height), xess_pass(gfx, width, height), dlss3_pass(gfx, width, height),
		tonemap_pass(width, height), fxaa_pass(width, height)
	{
		ray_tracing_supported = gfx->GetCapabilities().SupportsRayTracing();
		AddRenderResolutionChangedCallback(&PostProcessor::OnRenderResolutionChanged, *this);
	}

	void PostProcessor::AddPasses(RenderGraph& rg)
	{
		PostprocessorGUI();

		auto lights = reg.view<Light>();
		if (NeedsVelocityBuffer())
		{
			velocity_buffer_pass.AddPass(rg);
		}
		final_resource = AddHDRCopyPass(rg);

		for (entt::entity light : lights)
		{
			auto const& light_data = lights.get<Light>(light);
			if (!light_data.active || !light_data.lens_flare) continue;
			lens_flare_pass.AddPass2(rg, light_data);
		}
		for (entt::entity light : lights)
		{
			auto const& light_data = lights.get<Light>(light);
			if (!light_data.active) continue;

			if (light_data.type == LightType::Directional)
			{
				AddSunPass(rg, light);
				if (light_data.god_rays)
				{
					god_rays_pass.AddPass(rg, light_data);
					copy_to_texture_pass.AddPass(rg, final_resource, RG_RES_NAME(GodRaysOutput), BlendMode::AdditiveBlend);
				}
				else
				{
					copy_to_texture_pass.AddPass(rg, final_resource, RG_RES_NAME(SunOutput), BlendMode::AdditiveBlend);
				}
				break;
			}
		}

		if (clouds) clouds_pass.AddPass(rg);

		if (reflections == Reflections::SSR) final_resource = ssr_pass.AddPass(rg, final_resource);
		else if (reflections == Reflections::RTR)
		{
			copy_to_texture_pass.AddPass(rg, final_resource, RG_RES_NAME(RTR_Output), BlendMode::AdditiveBlend);
		}
		if (fog) final_resource = fog_pass.AddPass(rg, final_resource);

		switch (upscaler)
		{
		case UpscalerType::FSR2: final_resource  = fsr2_pass.AddPass(rg, final_resource); break;
		case UpscalerType::XeSS: final_resource  = xess_pass.AddPass(rg, final_resource); break;
		case UpscalerType::DLSS3: final_resource = dlss3_pass.AddPass(rg, final_resource); break;
		case UpscalerType::None:
		{
			if (HasAnyFlag(anti_aliasing, AntiAliasing_TAA))
			{
				rg.ImportTexture(RG_RES_NAME(HistoryBuffer), history_buffer.get());
				final_resource = taa_pass.AddPass(rg, final_resource, RG_RES_NAME(HistoryBuffer));
				rg.ExportTexture(final_resource, history_buffer.get());
			}
		}
		break;
		}

		if (dof)
		{
			blur_pass.SetResolution(display_width, display_height);
			blur_pass.AddPass(rg, final_resource, RG_RES_NAME(BlurredDofInput), " DoF ");
			blur_pass.SetResolution(render_width, render_height);
			if (bokeh) bokeh_pass.AddPass(rg, final_resource);
			final_resource = dof_pass.AddPass(rg, final_resource);
		}
		if (motion_blur) final_resource = motion_blur_pass.AddPass(rg, final_resource);
		if (automatic_exposure) automatic_exposure_pass.AddPasses(rg, final_resource);
		if (bloom) bloom_pass.AddPass(rg, final_resource);

		if (HasAnyFlag(anti_aliasing, AntiAliasing_FXAA))
		{
			tonemap_pass.AddPass(rg, final_resource, RG_RES_NAME(TonemapOutput));
			fxaa_pass.AddPass(rg, RG_RES_NAME(TonemapOutput));
		}
		else
		{
			tonemap_pass.AddPass(rg, final_resource);
		}
	}

	void PostProcessor::AddTonemapPass(RenderGraph& rg, RGResourceName input)
	{
		tonemap_pass.AddPass(rg, input);
	}

	void PostProcessor::OnResize(uint32 w, uint32 h)
	{
		display_width = w, display_height = h;
		switch (upscaler)
		{
		case UpscalerType::FSR2:  fsr2_pass.OnResize(w, h); break;
		case UpscalerType::XeSS:  xess_pass.OnResize(w, h); break;
		case UpscalerType::DLSS3: dlss3_pass.OnResize(w, h); break;
		case UpscalerType::None:  upscaler_disabled_event.Broadcast(display_width, display_height); break;
		}

		taa_pass.OnResize(w, h);
		motion_blur_pass.OnResize(w, h);
		dof_pass.OnResize(w, h);
		bokeh_pass.OnResize(w, h);
		bloom_pass.OnResize(w, h);
		automatic_exposure_pass.OnResize(w, h);
		fxaa_pass.OnResize(w, h);
		tonemap_pass.OnResize(w, h);

		if (history_buffer)
		{
			GfxTextureDesc render_target_desc = history_buffer->GetDesc();
			render_target_desc.width = display_width;
			render_target_desc.height = display_height;
			history_buffer = gfx->CreateTexture(render_target_desc);
		}
	}

	void PostProcessor::OnRenderResolutionChanged(uint32 w, uint32 h)
	{
		render_width = w, render_height = h;

		clouds_pass.OnResize(gfx, w, h);
		blur_pass.OnResize(w, h);
		add_textures_pass.OnResize(w, h);
		copy_to_texture_pass.OnResize(w, h);
		lens_flare_pass.OnResize(w, h);
		ssr_pass.OnResize(w, h);
		fog_pass.OnResize(w, h);
		velocity_buffer_pass.OnResize(w, h);
		god_rays_pass.OnResize(w, h);
	}

	void PostProcessor::OnSceneInitialized()
	{
		automatic_exposure_pass.OnSceneInitialized(gfx);
		clouds_pass.OnSceneInitialized(gfx);
		bokeh_pass.OnSceneInitialized(gfx);
		lens_flare_pass.OnSceneInitialized();
		tonemap_pass.OnSceneInitialized(); 

		GfxTextureDesc render_target_desc{};
		render_target_desc.format = GfxFormat::R16G16B16A16_FLOAT;
		render_target_desc.width = display_width;
		render_target_desc.height = display_height;
		render_target_desc.bind_flags = GfxBindFlag::ShaderResource;
		render_target_desc.initial_state = GfxResourceState::CopyDest;
		history_buffer = gfx->CreateTexture(render_target_desc);
	}

	RGResourceName PostProcessor::GetFinalResource() const
	{
		return final_resource;
	}

	bool PostProcessor::HasTAA() const
	{
		return HasAnyFlag(anti_aliasing, AntiAliasing_TAA);
	}

	RGResourceName PostProcessor::AddHDRCopyPass(RenderGraph& rg)
	{
		struct CopyPassData
		{
			RGTextureCopySrcId copy_src;
			RGTextureCopyDstId copy_dst;
		};

		rg.AddPass<CopyPassData>("Copy HDR Pass",
			[=](CopyPassData& data, RenderGraphBuilder& builder)
			{
				RGTextureDesc postprocess_desc{};
				postprocess_desc.width = render_width;
				postprocess_desc.height = render_height;
				postprocess_desc.format = GfxFormat::R16G16B16A16_FLOAT;

				builder.DeclareTexture(RG_RES_NAME(PostprocessMain), postprocess_desc);
				data.copy_dst = builder.WriteCopyDstTexture(RG_RES_NAME(PostprocessMain));
				data.copy_src = builder.ReadCopySrcTexture(RG_RES_NAME(HDR_RenderTarget));
			},
			[=](CopyPassData const& data, RenderGraphContext& context, GfxCommandList* cmd_list)
			{
				GfxTexture const& src_texture = context.GetCopySrcTexture(data.copy_src);
				GfxTexture& dst_texture = context.GetCopyDstTexture(data.copy_dst);
				cmd_list->CopyTexture(dst_texture, src_texture);
			}, RGPassType::Copy, RGPassFlags::None);

		return RG_RES_NAME(PostprocessMain);
	}

	void PostProcessor::AddSunPass(RenderGraph& rg, entt::entity sun)
	{
		FrameBlackboardData const& frame_data = rg.GetBlackboard().Get<FrameBlackboardData>();
		RGResourceName last_resource = final_resource;

		rg.AddPass<void>("Sun Pass",
			[=](RenderGraphBuilder& builder)
			{
				RGTextureDesc sun_output_desc{};
				sun_output_desc.format = GfxFormat::R16G16B16A16_FLOAT;
				sun_output_desc.width = render_width;
				sun_output_desc.height = render_height;
				sun_output_desc.clear_value = GfxClearValue(0.0f, 0.0f, 0.0f, 0.0f);

				builder.DeclareTexture(RG_RES_NAME(SunOutput), sun_output_desc);
				builder.ReadDepthStencil(RG_RES_NAME(DepthStencil), RGLoadStoreAccessOp::Preserve_Preserve);
				builder.WriteRenderTarget(RG_RES_NAME(SunOutput), RGLoadStoreAccessOp::Clear_Preserve);
				builder.SetViewport(render_width, render_height);
			},
			[=](RenderGraphContext& context, GfxCommandList* cmd_list)
			{
				GfxDevice* gfx = cmd_list->GetDevice();
				auto dynamic_allocator = gfx->GetDynamicAllocator();

				cmd_list->SetPipelineState(PSOCache::Get(GfxPipelineStateID::Sun));
				cmd_list->SetRootCBV(0, frame_data.frame_cbuffer_address);
				auto [transform, mesh, material] = reg.get<Transform, SubMesh, Material>(sun);

				struct Constants
				{
					Matrix model_matrix;
					Vector3 diffuse_color;
					uint32   diffuse_idx;
				} constants =
				{
					.model_matrix = transform.current_transform,
					.diffuse_color = Vector3(material.base_color),
					.diffuse_idx = (uint32)material.albedo_texture
				};
				GfxDynamicAllocation allocation = dynamic_allocator->AllocateCBuffer<Constants>();
				allocation.Update(constants);
				cmd_list->SetRootCBV(2, allocation.gpu_address);
				Draw(mesh, cmd_list);
			}, RGPassType::Graphics, RGPassFlags::None);
	}

	void PostProcessor::PostprocessorGUI()
	{
		GUI_RunCommand([&]()
			{
				int& current_upscaler = cvars::upscaler.Get();
				int& current_reflection_type = cvars::reflections.Get();
				if (ImGui::TreeNode("Post-processing"))
				{
					if (ImGui::Combo("Upscaler", &current_upscaler, "None\0FSR2\0XeSS\0DLSS3\0", 4))
					{
						upscaler = static_cast<UpscalerType>(current_upscaler);
						if (upscaler == UpscalerType::DLSS3 && !dlss3_pass.IsSupported())
						{
							upscaler = UpscalerType::None;
							ADRIA_LOG(WARNING, "DLSS3 is not supported on this device!");
						}

						switch (upscaler)
						{
						case UpscalerType::FSR2:  fsr2_pass.OnResize(display_width, display_height); break;
						case UpscalerType::XeSS:  xess_pass.OnResize(display_width, display_height); break;
						case UpscalerType::DLSS3: dlss3_pass.OnResize(display_width, display_height); break;
						case UpscalerType::None: upscaler_disabled_event.Broadcast(display_width, display_height); break;
						}
					}
					if (ImGui::Combo("Reflections", &current_reflection_type, "None\0SSR\0RTR\0", 3))
					{
						if (!ray_tracing_supported && current_reflection_type == 3) current_reflection_type = 1;
						reflections = static_cast<Reflections>(current_reflection_type);
					}

					ImGui::Checkbox("Automatic Exposure", &cvars::exposure.Get());

					ImGui::Checkbox("Volumetric Clouds", &cvars::clouds.Get());
					ImGui::Checkbox("Depth of Field", &cvars::dof.Get());
					if (cvars::dof) ImGui::Checkbox("Bokeh", &cvars::bokeh.Get());
					ImGui::Checkbox("Bloom", &cvars::bloom.Get());
					ImGui::Checkbox("Motion Blur", &cvars::motion_blur.Get());
					ImGui::Checkbox("Fog", &cvars::fog.Get());

					if (ImGui::TreeNode("Anti-Aliasing"))
					{
						ImGui::Checkbox("FXAA", &cvars::fxaa.Get());
						ImGui::Checkbox("TAA", &cvars::taa.Get());
						ImGui::TreePop();
					}

					ImGui::TreePop();
				}
				
				automatic_exposure = cvars::exposure;
				clouds = cvars::clouds;
				dof = cvars::dof;
				bokeh = cvars::bokeh;
				bloom = cvars::bloom;
				motion_blur = cvars::motion_blur;
				fog = cvars::fog;
				if (cvars::fxaa) anti_aliasing = static_cast<AntiAliasing>(anti_aliasing | AntiAliasing_FXAA);
				else anti_aliasing = static_cast<AntiAliasing>(anti_aliasing & (~AntiAliasing_FXAA));

				if (cvars::taa) anti_aliasing = static_cast<AntiAliasing>(anti_aliasing | AntiAliasing_TAA);
				else anti_aliasing = static_cast<AntiAliasing>(anti_aliasing & (~AntiAliasing_TAA));

			});
	}

}

