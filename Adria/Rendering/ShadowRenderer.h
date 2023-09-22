#pragma once
#include <array>
#include "Events/Delegate.h"
#include "RayTracedShadowsPass.h"
#include "Graphics/GfxDefines.h"
#include "Graphics/GfxDescriptor.h"

namespace adria
{
	class GfxDevice;
	class GfxCommandList;
	class GfxBuffer;
	class GfxTexture;
	class RenderGraph;
	class Camera;
	struct FrameCBuffer;


	DECLARE_EVENT(ShadowTextureRenderedEvent, ShadowRenderer, RGResourceName);

	class ShadowRenderer
	{
		static constexpr uint32 SHADOW_MAP_SIZE = 2048;
		static constexpr uint32 SHADOW_CUBE_SIZE = 512;
		static constexpr uint32 SHADOW_CASCADE_MAP_SIZE = 1024;
		static constexpr uint32 SHADOW_CASCADE_COUNT = 4;

	public:
		ShadowRenderer(entt::registry& reg, GfxDevice* gfx, uint32 width, uint32 height);
		~ShadowRenderer();

		void OnResize(uint32 w, uint32 h)
		{
			if (width != w || height != h)
			{
				width = w, height = h;
				ray_traced_shadows_pass.OnResize(w, h);
				light_mask_textures.clear();
			}
		}
		void SetupShadows(Camera const* camera);

		void AddShadowMapPasses(RenderGraph& rg);
		void AddRayTracingShadowPasses(RenderGraph& rg);

		void FillFrameCBuffer(FrameCBuffer& frame_cbuffer);

		ShadowTextureRenderedEvent& GetShadowTextureRenderedEvent() { return shadow_rendered_event; }

	private:
		entt::registry& reg;
		GfxDevice* gfx;
		uint32 width;
		uint32 height;
		RayTracedShadowsPass ray_traced_shadows_pass;

		std::unique_ptr<GfxBuffer>  light_matrices_buffer;
		GfxDescriptor				light_matrices_buffer_srvs[GFX_BACKBUFFER_COUNT];
		std::unordered_map<size_t, std::vector<std::unique_ptr<GfxTexture>>> light_shadow_maps;
		std::unordered_map<size_t, std::vector<GfxDescriptor>> light_shadow_map_srvs;
		std::unordered_map<size_t, std::vector<GfxDescriptor>> light_shadow_map_dsvs;
		std::unordered_map<size_t, std::unique_ptr<GfxTexture>> light_mask_textures;
		std::unordered_map<size_t, GfxDescriptor> light_mask_texture_srvs;
		std::unordered_map<size_t, GfxDescriptor> light_mask_texture_uavs;
		int32						   light_matrices_gpu_index = -1;

		std::vector<Matrix>								light_matrices;
		std::array<float, SHADOW_CASCADE_COUNT>		    split_distances{};
		float											cascades_split_lambda = 0.5f;

		ShadowTextureRenderedEvent shadow_rendered_event;

	private:
		void ShadowMapPass_Common(GfxDevice* gfx, GfxCommandList* cmd_list, size_t light_index, size_t matrix_index, size_t matrix_offset);
		static std::array<Matrix, SHADOW_CASCADE_COUNT> RecalculateProjectionMatrices(Camera const& camera, float split_lambda, std::array<float, SHADOW_CASCADE_COUNT>& split_distances);
	};
}