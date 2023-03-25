#pragma once
#include <memory>
#include "../Core/CoreTypes.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_impl_dx12.h"

namespace adria
{

	class GfxDevice;
	template<bool>
	class GfxRingDescriptorAllocator;

	using GUIDescriptorAllocator = GfxRingDescriptorAllocator<false>;

	struct WindowMessage;

	class GUI
	{
	public:
		GUI(GfxDevice* gfx);

		~GUI();

		void Begin() const;

		void End(ID3D12GraphicsCommandList* cmd_list) const;

		void HandleWindowMessage(WindowMessage const&) const;

		void ToggleVisibility();

		bool IsVisible() const;

		GUIDescriptorAllocator* DescriptorAllocator() const;

	private:
		GfxDevice* gfx;
		std::unique_ptr<GUIDescriptorAllocator> imgui_allocator;
		bool visible = true;
		mutable uint64 frame_count = 0;
	};

}