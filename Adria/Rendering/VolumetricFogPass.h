#pragma once
#include "TextureHandle.h"
#include "Graphics/GfxDescriptor.h"
#include "entt/entity/fwd.hpp"


namespace adria
{
	class RenderGraph;
	class GfxDevice;
	class GfxTexture;
	class GfxBuffer;

	class VolumetricFogPass
	{
		struct FogVolumeGPU
		{
			Vector3 center;
			Vector3 extents;
			Vector3 color;
			float   density_base;
			float   density_change;
		};

		struct FogVolume
		{
			BoundingBox volume;
			Color		color;
			float       density_base;
			float       density_change;
		};

		static constexpr uint32 BLUE_NOISE_TEXTURE_COUNT = 16;

	public:

		VolumetricFogPass(GfxDevice* gfx, entt::registry& reg, uint32 w, uint32 h);
		void AddPasses(RenderGraph& rendergraph);
		void OnResize(uint32 w, uint32 h)
		{
			width = w, height = h;
			CreateVoxelTexture();
		}

		void OnSceneInitialized();

	private:
		GfxDevice* gfx;
		entt::registry& reg;
		uint32 width, height;

		std::unique_ptr<GfxTexture> voxel_grid_history;
		GfxDescriptor voxel_grid_history_srv;
		uint32 voxel_grid_history_idx;

		std::vector<FogVolume> fog_volumes;
		std::unique_ptr<GfxBuffer> fog_volume_buffer;
		GfxDescriptor fog_volume_buffer_srv;
		uint32 fog_volume_buffer_idx;

		std::array<TextureHandle, BLUE_NOISE_TEXTURE_COUNT> blue_noise_handles;
		bool temporal_accumulation = false;

	private:

		void CreateVoxelTexture();
		void CreateFogVolumeBuffer();

		void AddLightInjectionPass(RenderGraph& rendergraph);
		void AddScatteringAccumulationPass(RenderGraph& rendergraph);
	};
}