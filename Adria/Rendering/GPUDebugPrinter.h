#pragma once
#include <memory>
#include "Graphics/GfxDefines.h"
#include "Graphics/GfxDescriptor.h"

namespace adria
{
	class GfxDevice;
	class GfxBuffer;

	class GPUDebugPrinter
	{
	public:
		explicit GPUDebugPrinter(GfxDevice* gfx);
		ADRIA_NONCOPYABLE(GPUDebugPrinter)
		ADRIA_DEFAULT_MOVABLE(GPUDebugPrinter)
		~GPUDebugPrinter();

		int32 GetPrintfBufferIndex();
		void Print();

	private:
		GfxDevice* gfx;
		std::unique_ptr<GfxBuffer> printf_buffer;
		std::unique_ptr<GfxBuffer> readback_buffers[GFX_BACKBUFFER_COUNT];
		GfxDescriptor srv_descriptor;
		GfxDescriptor uav_descriptor;
	};
}