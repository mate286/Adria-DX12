#pragma once
#include "Utilities/Delegate.h"

namespace adria
{
	class GfxDevice;
	class GfxShader;
	class GfxShaderKey;

	enum ShaderID : uint8
	{
		ShaderID_Invalid,
		VS_Sky,
		PS_Sky,
		CS_MinimalAtmosphereSky,
		CS_HosekWilkieSky,
		VS_FullscreenTriangle,
		VS_GBuffer,
		PS_GBuffer,
		VS_Shadow,
		PS_Shadow,
		VS_LensFlare,
		GS_LensFlare,
		PS_LensFlare,
		CS_LensFlare2,
		VS_Bokeh,
		GS_Bokeh,
		PS_Bokeh,
		PS_Copy,
		PS_Add,
		VS_Sun,
		VS_Simple,
		PS_Texture,
		PS_Solid,
		VS_Debug,
		PS_Debug,
		CS_Blur_Horizontal,
		CS_Blur_Vertical,
		CS_BloomDownsample,
		CS_BloomUpsample,
		CS_BokehGeneration,
		CS_GenerateMips,
		CS_FFT_Horizontal,
		CS_FFT_Vertical,
		CS_InitialSpectrum,
		CS_OceanNormals,
		CS_Phase,
		CS_Spectrum,
		VS_Ocean,
		PS_Ocean,
		VS_Decals,
		PS_Decals,
		VS_OceanLOD,
		DS_OceanLOD,
		HS_OceanLOD,
		VS_Rain,
		PS_Rain,
		CS_RainSimulation,
		VS_RainBlocker,
		CS_Picking,
		CS_BuildHistogram,
		CS_HistogramReduction,
		CS_Exposure,
		CS_Ssao,
		CS_Hbao,
		CS_Ssr,
		CS_ExponentialHeightFog,
		CS_Tonemap,
		CS_MotionVectors,
		CS_MotionBlur,
		CS_Dof,
		CS_Fxaa,
		CS_GodRays,
		CS_FilmEffects,
		CS_Ambient,
		CS_Clouds,
		CS_CloudDetail,
		CS_CloudShape,
		CS_CloudType,
		VS_CloudsCombine,
		PS_CloudsCombine,
		CS_Taa,
		CS_DeferredLighting,
		CS_VolumetricLighting,
		CS_TiledDeferredLighting,
		CS_ClusteredDeferredLighting,
		CS_ClusterBuilding,
		CS_ClusterCulling,
		CS_ClearCounters,
		CS_CullInstances,
		CS_BuildMeshletCullArgs,
		CS_CullMeshlets,
		CS_BuildMeshletDrawArgs,
		MS_DrawMeshlets,
		PS_DrawMeshlets,
		CS_BuildInstanceCullArgs,
		CS_InitializeHZB,
		CS_HZBMips,
		CS_RTAOFilter,
		CS_DDGIUpdateIrradiance,
		CS_DDGIUpdateDistance,
		VS_DDGIVisualize,
		PS_DDGIVisualize,
		CS_VolumetricFog_LightInjection,
		CS_VolumetricFog_ScatteringIntegration,
		PS_VolumetricFog_CombineFog,
		CS_ReSTIRGI_InitialSampling,
		LIB_DDGIRayTracing,
		LIB_Shadows,
		LIB_AmbientOcclusion,
		LIB_Reflections,
		LIB_PathTracing,
		ShaderId_Count
	};

	DECLARE_MULTICAST_DELEGATE(ShaderRecompiledEvent, GfxShaderKey const&)
	DECLARE_MULTICAST_DELEGATE(LibraryRecompiledEvent, GfxShaderKey const&)
	class ShaderManager
	{
	public:
		static void Initialize();
		static void Destroy();
		static void CheckIfShadersHaveChanged();

		static ShaderRecompiledEvent& GetShaderRecompiledEvent();
		static LibraryRecompiledEvent& GetLibraryRecompiledEvent();
		static GfxShader const& GetGfxShader(GfxShaderKey const& shader_key);
	};
	#define GetGfxShader(key) ShaderManager::GetGfxShader(key)
}