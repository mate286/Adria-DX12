#pragma once
#include <DirectXCollision.h>
#include <memory>
#include <optional>
#include "RendererSettings.h"
#include "SceneViewport.h"
#include "Picker.h"
#include "ConstantBuffers.h"
#include "ParticleRenderer.h"
#include "RayTracer.h"
#include "../tecs/Registry.h"
#include "../Graphics/TextureManager.h"
#include "../Graphics/ShaderUtility.h"
#include "../Graphics/Texture2D.h"
#include "../Graphics/RenderPass.h"
#include "../Graphics/ConstantBuffer.h"
#include "../Graphics/TextureCube.h"
#include "../Graphics/Texture2DArray.h"
#include "../Graphics/VertexBuffer.h"
#include "../Graphics/IndexBuffer.h"
#include "../Graphics/StructuredBuffer.h"
#include "../Graphics/Profiler.h"


namespace adria
{
	class Camera;
	class GraphicsCoreDX12;
	template<typename T>
	class StructuredBuffer;
	struct Light;


	class Renderer
	{
		enum ENullHeapSlot
		{
			TEXTURE2D_SLOT,
			TEXTURECUBE_SLOT,
			TEXTURE2DARRAY_SLOT,
			RWTEXTURE2D_SLOT,
			NULL_HEAP_SIZE
		};

		enum EIBLHeapSlot
		{
			ENV_TEXTURE_SLOT,
			IRMAP_TEXTURE_SLOT,
			BRDF_LUT_TEXTURE_SLOT
		};

		static constexpr uint32 GBUFFER_SIZE = 3;
		static constexpr uint32 SSAO_NOISE_DIM = 8;
		static constexpr uint32 SSAO_KERNEL_SIZE = 16;
		static constexpr uint32 CLUSTER_SIZE_X = 16;
		static constexpr uint32 CLUSTER_SIZE_Y = 16;
		static constexpr uint32 CLUSTER_SIZE_Z = 16;
		static constexpr uint32 CLUSTER_COUNT = CLUSTER_SIZE_X * CLUSTER_SIZE_Y * CLUSTER_SIZE_Z;
		static constexpr uint32 CLUSTER_MAX_LIGHTS = 128;
		static constexpr uint32 RESOLUTION = 512;

	public:
		Renderer(tecs::registry& reg, GraphicsCoreDX12* gfx, uint32 width, uint32 height);
		
		~Renderer();

		void NewFrame(Camera const* camera);

		void Update(float32 dt);

		void SetSceneViewportData(SceneViewport&&);

		void SetProfilerSettings(ProfilerSettings);

		void Render(RendererSettings const&);

		void ResolveToBackbuffer();

		void ResolveToOffscreenFramebuffer();

		void OnResize(uint32 width, uint32 height);

		void OnRightMouseClicked();

		void UploadData();

		Texture2D GetOffscreenTexture() const;

		TextureManager& GetTextureManager();

		std::vector<std::string> GetProfilerResults(bool log = false);

		PickingData GetPickingData() const;

		Texture2D const& GetRayTracingShadowsTexture_Debug() const
		{
			return ray_tracer.GetRayTracingShadowsTexture();
		}

		Texture2D const& GetRayTracingAOTexture_Debug() const
		{
			return ray_tracer.GetRayTracingAmbientOcclusionTexture();
		}

	private:
		uint32 width, height;
		tecs::registry& reg;
		GraphicsCoreDX12* gfx;

		uint32 const backbuffer_count;
		uint32 backbuffer_index;
		TextureManager texture_manager;
		Camera const* camera;

		ParticleRenderer particle_renderer;
		RayTracer ray_tracer;

		RendererSettings settings;
		Profiler profiler;
		ProfilerSettings profiler_settings;

		SceneViewport current_scene_viewport;
		Picker picker;
		PickingData picking_data;
		bool update_picking_data = false;

		std::unordered_map<EShader, ShaderBlob> shader_map;
		std::unordered_map<ERootSignature, Microsoft::WRL::ComPtr<ID3D12RootSignature>> rs_map;
		std::unordered_map<EPipelineStateObject, Microsoft::WRL::ComPtr<ID3D12PipelineState>> pso_map;

		//textures and heaps
		Texture2D hdr_render_target;
		Texture2D prev_hdr_render_target;
		Texture2D depth_target;
		Texture2D ldr_render_target;
		Texture2D offscreen_ldr_target;
		std::vector<Texture2D> gbuffer;
		Texture2D shadow_depth_map;
		TextureCube shadow_depth_cubemap;
		Texture2DArray shadow_depth_cascades;
		Texture2D ao_texture;
		Texture2D hbao_random_texture;
		Texture2D ssao_random_texture;
		Texture2D velocity_buffer;
		Texture2D blur_intermediate_texture;
		Texture2D blur_final_texture;
		Texture2D bloom_extract_texture;
		Texture2D uav_target;
		Texture2D debug_tiled_texture;
		std::array<Texture2D, 2> postprocess_textures;
		bool postprocess_index = false;
		std::array<Texture2D, 2> ping_pong_phase_textures;
		bool pong_phase = false;
		std::array<Texture2D, 2> ping_pong_spectrum_textures;
		bool pong_spectrum = false;
		Texture2D ocean_normal_map;
		Texture2D ocean_initial_spectrum;

		std::unique_ptr<DescriptorHeap> rtv_heap;
		std::unique_ptr<DescriptorHeap> srv_heap;
		std::unique_ptr<DescriptorHeap> dsv_heap;
		std::unique_ptr<DescriptorHeap> uav_heap;
		std::unique_ptr<DescriptorHeap> null_srv_heap; 
		std::unique_ptr<DescriptorHeap> null_uav_heap;
		uint32 srv_heap_index = 0;
		uint32 uav_heap_index = 0;
		uint32 rtv_heap_index = 0;
		uint32 dsv_heap_index = 0;
		std::unique_ptr<DescriptorHeap> constant_srv_heap;
		std::unique_ptr<DescriptorHeap> constant_dsv_heap;
		std::unique_ptr<DescriptorHeap> constant_uav_heap;

		//Render Passes
		RenderPass gbuffer_render_pass;
		RenderPass decal_pass;
		RenderPass ssao_render_pass;
		RenderPass hbao_render_pass;
		RenderPass ambient_render_pass;
		RenderPass lighting_render_pass;
		RenderPass shadow_map_pass;
		std::array<RenderPass, 6> shadow_cubemap_passes;
		std::vector<RenderPass> shadow_cascades_passes;
		std::array<RenderPass, 2> postprocess_passes;
		RenderPass forward_render_pass;
		RenderPass particle_pass;
		RenderPass velocity_buffer_pass;
		RenderPass fxaa_render_pass;
		RenderPass offscreen_resolve_pass;

		//Persistent cbuffers
		ConstantBuffer<FrameCBuffer> frame_cbuffer;
		FrameCBuffer frame_cbuf_data;
		ConstantBuffer<PostprocessCBuffer> postprocess_cbuffer;
		PostprocessCBuffer postprocess_cbuf_data;
		ConstantBuffer<ComputeCBuffer> compute_cbuffer;
		ComputeCBuffer compute_cbuf_data;
		ConstantBuffer<WeatherCBuffer> weather_cbuffer;
		WeatherCBuffer weather_cbuf_data;
		
		//Persistent sbuffers
		StructuredBuffer<ClusterAABB>	clusters;
		StructuredBuffer<uint32>			light_counter;
		StructuredBuffer<uint32>			light_list;
		StructuredBuffer<LightGrid>  	light_grid;
		std::unique_ptr<StructuredBuffer<Bokeh>> bokeh;
		
		Texture2D sun_target;
		std::array<DirectX::XMVECTOR, 16> ssao_kernel{};
		DirectX::BoundingBox light_bounding_box;
		DirectX::BoundingFrustum light_bounding_frustum;
		std::optional<DirectX::BoundingSphere> scene_bounding_sphere = std::nullopt;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> lens_flare_textures;
		std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> clouds_textures;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> bokeh_command_signature;
		Microsoft::WRL::ComPtr<ID3D12Resource> bokeh_indirect_draw_buffer;
		Microsoft::WRL::ComPtr<ID3D12Resource> counter_reset_buffer;
		TEXTURE_HANDLE hex_bokeh_handle = INVALID_TEXTURE_HANDLE;
		TEXTURE_HANDLE oct_bokeh_handle = INVALID_TEXTURE_HANDLE;
		TEXTURE_HANDLE circle_bokeh_handle = INVALID_TEXTURE_HANDLE;
		TEXTURE_HANDLE cross_bokeh_handle = INVALID_TEXTURE_HANDLE;
		TEXTURE_HANDLE foam_handle = INVALID_TEXTURE_HANDLE;
		TEXTURE_HANDLE perlin_handle = INVALID_TEXTURE_HANDLE;

		bool recreate_clusters = true;

		std::unique_ptr<DescriptorHeap> ibl_heap;
		Microsoft::WRL::ComPtr<ID3D12Resource> env_texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> irmap_texture;
		Microsoft::WRL::ComPtr<ID3D12Resource> brdf_lut_texture;
	    bool ibl_textures_generated = false;

		std::shared_ptr<VertexBuffer>	cube_vb = nullptr;
		std::shared_ptr<IndexBuffer>	cube_ib = nullptr;
	private:

		void LoadShaders();
		void CreateRootSignatures();
		void CreatePipelineStateObjects();

		void CreateDescriptorHeaps();
		void CreateResolutionDependentResources(uint32 width, uint32 height);
		void CreateOtherResources();
		void CreateRenderPasses(uint32 width, uint32 height);

		void CreateIBLTextures();
		
		void UpdateConstantBuffers(float32 dt);
		void UpdateOcean(ID3D12GraphicsCommandList4* cmd_list);
		void UpdateParticles(float32 dt);
		void CameraFrustumCulling();
		void LightFrustumCulling(ELightType type);

		void PassPicking(ID3D12GraphicsCommandList4* cmd_list);
		void PassGBuffer(ID3D12GraphicsCommandList4* cmd_list);
		void PassDecals(ID3D12GraphicsCommandList4* cmd_list);
		void PassSSAO(ID3D12GraphicsCommandList4* cmd_list);
		void PassHBAO(ID3D12GraphicsCommandList4* cmd_list);
		void PassRTAO(ID3D12GraphicsCommandList4* cmd_list);
		void PassAmbient(ID3D12GraphicsCommandList4* cmd_list);
		void PassDeferredLighting(ID3D12GraphicsCommandList4* cmd_list); 
		void PassDeferredTiledLighting(ID3D12GraphicsCommandList4* cmd_list);
		void PassDeferredClusteredLighting(ID3D12GraphicsCommandList4* cmd_list);
		void PassForward(ID3D12GraphicsCommandList4* cmd_list); 
		void PassPostprocess(ID3D12GraphicsCommandList4* cmd_list);

		void PassShadowMapDirectional(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassShadowMapSpot(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassShadowMapPoint(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassShadowMapCascades(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassShadowMapCommon(ID3D12GraphicsCommandList4* cmd_list);
		void PassVolumetric(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		
		void PassForwardCommon(ID3D12GraphicsCommandList4* cmd_list, bool transparent);
		void PassSky(ID3D12GraphicsCommandList4* cmd_list);
		void PassOcean(ID3D12GraphicsCommandList4* cmd_list);
		void PassParticles(ID3D12GraphicsCommandList4* cmd_list);
		
		void PassLensFlare(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassVolumetricClouds(ID3D12GraphicsCommandList4* cmd_list);
		void PassSSR(ID3D12GraphicsCommandList4* cmd_list);
		void PassDepthOfField(ID3D12GraphicsCommandList4* cmd_list);
		void PassGenerateBokeh(ID3D12GraphicsCommandList4* cmd_list);
		void PassDrawBokeh(ID3D12GraphicsCommandList4* cmd_list);
		void PassBloom(ID3D12GraphicsCommandList4* cmd_list);
		void PassGodRays(ID3D12GraphicsCommandList4* cmd_list, Light const& light);
		void PassVelocityBuffer(ID3D12GraphicsCommandList4* cmd_list);
		void PassMotionBlur(ID3D12GraphicsCommandList4* cmd_list);
		void PassFXAA(ID3D12GraphicsCommandList4* cmd_list);
		void PassTAA(ID3D12GraphicsCommandList4* cmd_list);
		void PassFog(ID3D12GraphicsCommandList4* cmd_list);
		void PassToneMap(ID3D12GraphicsCommandList4* cmd_list);

		void DrawSun(ID3D12GraphicsCommandList4* cmd_list, tecs::entity sun);
		//result in blur final 
		void BlurTexture(ID3D12GraphicsCommandList4* cmd_list, Texture2D const& texture);
		//result in current render target
		void CopyTexture(ID3D12GraphicsCommandList4* cmd_list, Texture2D const& texture, EBlendMode mode = EBlendMode::None);
		
		void AddTextures(ID3D12GraphicsCommandList4* cmd_list, Texture2D const& texture1, Texture2D const& texture2, EBlendMode mode = EBlendMode::None);

		void GenerateMips(ID3D12GraphicsCommandList4* cmd_list, Texture2D const& texture,
			D3D12_RESOURCE_STATES start_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATES end_state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	};
}