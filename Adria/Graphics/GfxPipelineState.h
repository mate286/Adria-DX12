#pragma once
#include "GfxStates.h"
#include "GfxShader.h"
#include "GfxInputLayout.h"
#include "GfxResourceCommon.h"
#include "../Rendering/Enums.h"
#include "../Events/Delegate.h"

namespace adria
{
	class GfxDevice;

	struct GraphicsPipelineStateDesc
	{
		GfxRasterizerState rasterizer_state{};
		GfxBlendState blend_state{};
		GfxDepthStencilState depth_state{};
		GfxPrimitiveTopologyType topology_type = GfxPrimitiveTopologyType::Triangle;
		uint32 num_render_targets = 0;
		GfxFormat rtv_formats[8];
		GfxFormat dsv_format = GfxFormat::UNKNOWN;
		GfxInputLayout input_layout;
		GfxRootSignatureID root_signature = GfxRootSignatureID::Invalid;
		GfxShaderID VS = GfxShaderID_Invalid;
		GfxShaderID PS = GfxShaderID_Invalid;
		GfxShaderID DS = GfxShaderID_Invalid;
		GfxShaderID HS = GfxShaderID_Invalid;
		GfxShaderID GS = GfxShaderID_Invalid;
		uint32 sample_mask = UINT_MAX;
	};

	class GraphicsPipelineState
	{
	public:
		GraphicsPipelineState(GfxDevice* gfx, GraphicsPipelineStateDesc const& desc);
		~GraphicsPipelineState();

		operator ID3D12PipelineState*() const;

	private:
		GfxDevice* gfx;
		ArcPtr<ID3D12PipelineState> pso;
		GraphicsPipelineStateDesc desc;
		DelegateHandle event_handle;
	private:
		void OnShaderRecompiled(GfxShaderID s);
		void Create(GraphicsPipelineStateDesc const& desc);
	};

	struct ComputePipelineStateDesc
	{
		GfxRootSignatureID root_signature;
		GfxShaderID CS;
	};

	class ComputePipelineState
	{
	public:
		ComputePipelineState(GfxDevice* gfx, ComputePipelineStateDesc const& desc);
		~ComputePipelineState();

		operator ID3D12PipelineState*() const;

	private:
		GfxDevice* gfx;
		ArcPtr<ID3D12PipelineState> pso;
		ComputePipelineStateDesc desc;
		DelegateHandle event_handle;

	private:
		void OnShaderRecompiled(GfxShaderID s);
		void Create(ComputePipelineStateDesc const& desc);
	};
}