#pragma once
#include "Enums.h"
#include "../Core/Definitions.h"
#include "../RenderGraph/RenderGraphResourceName.h"
#include "../Graphics/CommandSignature.h"

namespace adria
{
	class RenderGraph;
	class TextureManager;
	class GraphicsDevice;
	class Buffer;

	class BokehPass
	{

		enum class EBokehType : uint8
		{
			Hex,
			Oct,
			Circle,
			Cross
		};

		struct BokehParameters
		{
			float bokeh_blur_threshold = 0.9f;
			float bokeh_lum_threshold = 1.0f;
			float bokeh_radius_scale = 25.0f;
			float bokeh_color_scale = 1.0f;
			float bokeh_fallout = 0.9f;
			EBokehType bokeh_type = EBokehType::Hex;
		};

	public:
		BokehPass(TextureManager& texture_manager, uint32 w, uint32 h);

		void AddPass(RenderGraph& rendergraph, RGResourceName input);
		void OnResize(uint32 w, uint32 h);
		void OnSceneInitialized(GraphicsDevice* gfx);

	private:
		TextureManager& texture_manager;
		uint32 width, height;
		BokehParameters params{};
		size_t hex_bokeh_handle = -1;
		size_t oct_bokeh_handle = -1;
		size_t circle_bokeh_handle = -1;
		size_t cross_bokeh_handle = -1;
		std::unique_ptr<Buffer> counter_reset_buffer;
		std::unique_ptr<Buffer> bokeh_indirect_buffer;
		std::unique_ptr<DrawIndirectSignature> bokeh_command_signature;
		
	private:
		void AddGenerateBokehPass(RenderGraph& rg, RGResourceName input);
		void AddDrawBokehPass(RenderGraph& rg, RGResourceName input);
	};
}