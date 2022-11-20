#include "RayTracedShadowsPass.h"
#include "BlackboardData.h"
#include "ShaderCache.h"
#include "PSOCache.h" 

#include "../RenderGraph/RenderGraph.h"

namespace adria
{

	RayTracedShadowsPass::RayTracedShadowsPass(GraphicsDevice* gfx, uint32 width, uint32 height)
		: gfx(gfx), width(width), height(height)
	{
		ID3D12Device* device = gfx->GetDevice();
		D3D12_FEATURE_DATA_D3D12_OPTIONS5 features5{};
		HRESULT hr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &features5, sizeof(D3D12_FEATURE_DATA_D3D12_OPTIONS5));
		is_supported = features5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0;
		if (IsSupported())
		{
			CreateStateObject();
			ShaderCache::GetLibraryRecompiledEvent().AddMember(&RayTracedShadowsPass::OnLibraryRecompiled, *this);
		}
	}

	void RayTracedShadowsPass::AddPass(RenderGraph& rg, uint32 light_index, RGResourceName mask_name)
	{
		if (!IsSupported()) return;

		GlobalBlackboardData const& global_data = rg.GetBlackboard().GetChecked<GlobalBlackboardData>();
		struct RayTracedShadowsPassData
		{
			RGTextureReadOnlyId depth;
			RGTextureReadWriteId mask;
		};

		rg.AddPass<RayTracedShadowsPassData>("Ray Traced Shadows Pass",
			[=](RayTracedShadowsPassData& data, RGBuilder& builder)
			{
				data.mask = builder.WriteTexture(mask_name);
				data.depth = builder.ReadTexture(RG_RES_NAME(DepthStencil), ReadAccess_NonPixelShader);
			},
			[=](RayTracedShadowsPassData const& data, RenderGraphContext& ctx, GraphicsDevice* gfx, CommandList* cmd_list)
			{
				auto device = gfx->GetDevice();
				auto descriptor_allocator = gfx->GetOnlineDescriptorAllocator();
				auto dynamic_allocator = gfx->GetDynamicAllocator();

				uint32 i = (uint32)descriptor_allocator->AllocateRange(2);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 0), ctx.GetReadOnlyTexture(data.depth), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				device->CopyDescriptorsSimple(1, descriptor_allocator->GetHandle(i + 1), ctx.GetReadWriteTexture(data.mask), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

				struct RayTracedShadowsConstants
				{
					uint32  depth_idx;
					uint32  output_idx;
					uint32  light_idx;
				} constants =
				{
					.depth_idx = i + 0, .output_idx = i + 1,
					.light_idx = light_index
				};
				
				cmd_list->SetPipelineState1(ray_traced_shadows.Get());

				cmd_list->SetComputeRootConstantBufferView(0, global_data.frame_cbuffer_address);
				cmd_list->SetComputeRoot32BitConstants(1, 3, &constants, 0);

				D3D12_DISPATCH_RAYS_DESC dispatch_desc{};
				dispatch_desc.Width = width;
				dispatch_desc.Height = height;
				dispatch_desc.Depth = 1;

				RayTracingShaderTable table(ray_traced_shadows.Get());
				table.SetRayGenShader("RTS_RayGen_Hard");
				table.AddMissShader("RTS_Miss", 0);
				table.AddHitGroup("ShadowAnyHitGroup", 0);
				table.Commit(*gfx->GetDynamicAllocator(), dispatch_desc);
				cmd_list->DispatchRays(&dispatch_desc);

			}, ERGPassType::Compute, ERGPassFlags::ForceNoCull);
	}

	void RayTracedShadowsPass::OnResize(uint32 w, uint32 h)
	{
		width = w, height = h;
	}

	bool RayTracedShadowsPass::IsSupported() const
	{
		return is_supported;
	}

	void RayTracedShadowsPass::CreateStateObject()
	{
		ID3D12Device5* device = gfx->GetDevice();

		Shader const& rt_shadows_blob = ShaderCache::GetShader(LIB_Shadows);
		Shader const& rt_soft_shadows_blob = ShaderCache::GetShader(LIB_SoftShadows);

		StateObjectBuilder rt_shadows_state_object_builder(6);
		{
			D3D12_EXPORT_DESC export_descs[] =
			{
				D3D12_EXPORT_DESC{.Name = L"RTS_RayGen_Hard", .ExportToRename = L"RTS_RayGen"},
				D3D12_EXPORT_DESC{.Name = L"RTS_AnyHit", .ExportToRename = NULL},
				D3D12_EXPORT_DESC{.Name = L"RTS_Miss", .ExportToRename = NULL}
			};

			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc{};
			dxil_lib_desc.DXILLibrary.BytecodeLength = rt_shadows_blob.GetLength();
			dxil_lib_desc.DXILLibrary.pShaderBytecode = rt_shadows_blob.GetPointer();
			dxil_lib_desc.NumExports = ARRAYSIZE(export_descs);
			dxil_lib_desc.pExports = export_descs;
			rt_shadows_state_object_builder.AddSubObject(dxil_lib_desc);

			D3D12_DXIL_LIBRARY_DESC	dxil_lib_desc2{};
			D3D12_EXPORT_DESC export_desc2{};
			export_desc2.ExportToRename = L"RTS_RayGen";
			export_desc2.Name = L"RTS_RayGen_Soft";
			dxil_lib_desc2.DXILLibrary.BytecodeLength = rt_soft_shadows_blob.GetLength();
			dxil_lib_desc2.DXILLibrary.pShaderBytecode = rt_soft_shadows_blob.GetPointer();
			dxil_lib_desc2.NumExports = 1;
			dxil_lib_desc2.pExports = &export_desc2;
			rt_shadows_state_object_builder.AddSubObject(dxil_lib_desc2);

			// Add a state subobject for the shader payload configuration
			D3D12_RAYTRACING_SHADER_CONFIG rt_shadows_shader_config{};
			rt_shadows_shader_config.MaxPayloadSizeInBytes = 4;	//bool in hlsl is 4 bytes
			rt_shadows_shader_config.MaxAttributeSizeInBytes = D3D12_RAYTRACING_MAX_ATTRIBUTE_SIZE_IN_BYTES;
			rt_shadows_state_object_builder.AddSubObject(rt_shadows_shader_config);

			D3D12_GLOBAL_ROOT_SIGNATURE global_root_sig{};
			global_root_sig.pGlobalRootSignature = gfx->GetCommonRootSignature();
			rt_shadows_state_object_builder.AddSubObject(global_root_sig);

			// Add a state subobject for the ray tracing pipeline config
			D3D12_RAYTRACING_PIPELINE_CONFIG pipeline_config = {};
			pipeline_config.MaxTraceRecursionDepth = 1;
			rt_shadows_state_object_builder.AddSubObject(pipeline_config);

			D3D12_HIT_GROUP_DESC anyhit_group{};
			anyhit_group.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
			anyhit_group.AnyHitShaderImport = L"RTS_AnyHit";
			anyhit_group.HitGroupExport = L"ShadowAnyHitGroup";
			rt_shadows_state_object_builder.AddSubObject(anyhit_group);

			ray_traced_shadows.Attach(rt_shadows_state_object_builder.CreateStateObject(device));
		}
	}

	void RayTracedShadowsPass::OnLibraryRecompiled(EShaderId shader)
	{
		if (shader == LIB_Shadows || shader == LIB_SoftShadows) CreateStateObject();
	}

}

