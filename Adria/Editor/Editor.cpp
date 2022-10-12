#include "Editor.h"
#include "GUICommand.h"
#include "nfd.h"
#include "../Rendering/Renderer.h"
#include "../Graphics/GraphicsDeviceDX12.h"
#include "../Rendering/ModelImporter.h"
#include "../Rendering/PipelineState.h"
#include "../Rendering/ShaderManager.h"
#include "../Logging/Logger.h"
#include "../Utilities/FilesUtil.h"
#include "../Utilities/StringUtil.h"
#include "../Utilities/Random.h"
#include "../Math/BoundingVolumeHelpers.h"
#include "pix3.h"

using namespace DirectX;

namespace adria
{
	//heavily based on AMD Cauldron code
	struct ProfilerState
	{
		bool  show_average;
		struct AccumulatedTimeStamp
		{
			float sum;
			float minimum;
			float maximum;

			AccumulatedTimeStamp()
				: sum(0.0f), minimum(FLT_MAX), maximum(0)
			{}
		};

		std::vector<AccumulatedTimeStamp> displayed_timestamps;
		std::vector<AccumulatedTimeStamp> accumulating_timestamps;
		float64 last_reset_time;
		uint32 accumulating_frame_count;
	};
	struct ImGuiLogger
	{
		ImGuiTextBuffer     Buf;
		ImGuiTextFilter     Filter;
		ImVector<int>       LineOffsets;
		bool                AutoScroll;

		ImGuiLogger()
		{
			AutoScroll = true;
			Clear();
		}

		void Clear()
		{
			Buf.clear();
			LineOffsets.clear();
			LineOffsets.push_back(0);
		}

		void AddLog(const char* fmt, ...) IM_FMTARGS(2)
		{
			int old_size = Buf.size();
			va_list args;
			va_start(args, fmt);
			Buf.appendfv(fmt, args);
			va_end(args);
			for (int new_size = Buf.size(); old_size < new_size; old_size++)
				if (Buf[old_size] == '\n')
					LineOffsets.push_back(old_size + 1);
		}

		void Draw(const char* title, bool* p_open = NULL)
		{
			if (!ImGui::Begin(title, p_open))
			{
				ImGui::End();
				return;
			}

			// Options menu
			if (ImGui::BeginPopup("Options"))
			{
				ImGui::Checkbox("Auto-scroll", &AutoScroll);
				ImGui::EndPopup();
			}

			// Main window
			if (ImGui::Button("Options"))
				ImGui::OpenPopup("Options");
			ImGui::SameLine();
			bool clear = ImGui::Button("Clear");
			ImGui::SameLine();
			bool copy = ImGui::Button("Copy");
			ImGui::SameLine();
			Filter.Draw("Filter", -100.0f);

			ImGui::Separator();
			ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

			if (clear)
				Clear();
			if (copy)
				ImGui::LogToClipboard();

			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
			const char* buf = Buf.begin();
			const char* buf_end = Buf.end();
			if (Filter.IsActive())
			{
				for (int line_no = 0; line_no < LineOffsets.Size; line_no++)
				{
					const char* line_start = buf + LineOffsets[line_no];
					const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
					if (Filter.PassFilter(line_start, line_end))
						ImGui::TextUnformatted(line_start, line_end);
				}
			}
			else
			{
				ImGuiListClipper clipper;
				clipper.Begin(LineOffsets.Size);
				while (clipper.Step())
				{
					for (int line_no = clipper.DisplayStart; line_no < clipper.DisplayEnd; line_no++)
					{
						const char* line_start = buf + LineOffsets[line_no];
						const char* line_end = (line_no + 1 < LineOffsets.Size) ? (buf + LineOffsets[line_no + 1] - 1) : buf_end;
						ImGui::TextUnformatted(line_start, line_end);
					}
				}
				clipper.End();
			}
			ImGui::PopStyleVar();

			if (AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);

			ImGui::EndChild();
			ImGui::End();
		}
	};
	class EditorLogger : public ILogger
	{
	public:
		EditorLogger(ImGuiLogger* logger, ELogLevel logger_level = ELogLevel::LOG_DEBUG) : logger{ logger }, logger_level{ logger_level } {}

		virtual void Log(ELogLevel level, char const* entry, char const* file, uint32_t line) override
		{
			if (level < logger_level) return;
			std::string log_entry = GetLogTime() + LevelToString(level) + std::string(entry) + "\n";
			if (logger) logger->AddLog(log_entry.c_str());
		}
	private:
		ImGuiLogger* logger;
		ELogLevel logger_level;
	};

	Editor::Editor() = default;
	Editor::~Editor() = default;
	void Editor::Init(EditorInit&& init)
	{
		editor_log = std::make_unique<ImGuiLogger>();
		ADRIA_REGISTER_LOGGER(new EditorLogger(editor_log.get()));
		engine = std::make_unique<Engine>(init.engine_init);
		gui = std::make_unique<GUI>(engine->gfx.get());
		engine->RegisterEditorEventCallbacks(editor_events);
		SetStyle();
	}
	void Editor::Destroy()
	{
		while (!aabb_updates.empty()) aabb_updates.pop();
		gui.reset();
		engine.reset();
		editor_log.reset();
	}
	void Editor::HandleWindowMessage(WindowMessage const& msg_data)
	{
		engine->HandleWindowMessage(msg_data);
		gui->HandleWindowMessage(msg_data);
	}
	void Editor::Run()
	{
		HandleInput();
		renderer_settings.gui_visible = gui->IsVisible();
		if (gui->IsVisible())
		{
			engine->SetViewportData(viewport_data);
			engine->Run(renderer_settings);
			auto gui_cmd_list = engine->gfx->GetDefaultCommandList();
			engine->gfx->SetBackbuffer(gui_cmd_list);
			{
				PIXScopedEvent(gui_cmd_list, PIX_COLOR_DEFAULT, "GUI Pass");
				gui->Begin();
				MenuBar();
				ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
				Scene();
				ListEntities();
				AddEntities();
				Settings();
				Camera();
				Properties();
				Log();
				Profiling();
				ShaderHotReload();
				if (engine->renderer->IsRayTracingSupported()) RayTracingDebug();
				gui->End(gui_cmd_list);
			}
			if (!aabb_updates.empty())
			{
				engine->gfx->WaitForGPU();
				while (!aabb_updates.empty())
				{
					AABB* aabb = aabb_updates.front();
					aabb->UpdateBuffer(engine->gfx.get());
					aabb_updates.pop();
				}
			}
			engine->Present();
		}
		else
		{
			engine->SetViewportData(std::nullopt);
			engine->Run(renderer_settings);
			engine->Present();
		}

		if (reload_shaders)
		{
			engine->gfx->WaitForGPU();
			ShaderCache::CheckIfShadersHaveChanged();
			reload_shaders = false;
		}
	}
	void Editor::AddCommand(GUICommand&& command)
	{
		commands.emplace_back(std::move(command));
	}

	void Editor::SetStyle()
	{
		ImGuiStyle& style = ImGui::GetStyle();

		style.FrameRounding = 0.0f;
		style.GrabRounding = 1.0f;
		style.WindowRounding = 0.0f;
		style.IndentSpacing = 10.0f;
		style.ScrollbarSize = 16.0f;
		style.WindowPadding = ImVec2(5, 5);
		style.FramePadding = ImVec2(2, 2);

		ImVec4* colors = ImGui::GetStyle().Colors;
		colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
		colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);
		colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
		colors[ImGuiCol_ChildBg] = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
		colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
		colors[ImGuiCol_Border] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_FrameBgHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_TitleBg] = ImVec4(0.07f, 0.07f, 0.07f, 1.00f);
		colors[ImGuiCol_TitleBgActive] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
		colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		colors[ImGuiCol_ScrollbarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.39f);
		colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
		colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		colors[ImGuiCol_CheckMark] = ImVec4(0.14f, 0.71f, 0.83f, 0.95f);
		colors[ImGuiCol_SliderGrab] = ImVec4(0.26f, 0.67f, 0.82f, 0.83f);
		colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.80f, 0.96f, 1.00f);
		colors[ImGuiCol_Button] = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);
		colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_ButtonActive] = ImVec4(0.37f, 0.37f, 0.37f, 1.00f);
		colors[ImGuiCol_Header] = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
		colors[ImGuiCol_HeaderHovered] = ImVec4(0.35f, 0.35f, 0.35f, 0.58f);
		colors[ImGuiCol_HeaderActive] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
		colors[ImGuiCol_Separator] = ImVec4(0.20f, 0.25f, 0.29f, 1.00f);
		colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
		colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
		colors[ImGuiCol_ResizeGrip] = ImVec4(1.00f, 1.00f, 1.00f, 0.23f);
		colors[ImGuiCol_ResizeGripHovered] = ImVec4(1.00f, 1.00f, 1.00f, 0.67f);
		colors[ImGuiCol_ResizeGripActive] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
		colors[ImGuiCol_Tab] = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
		colors[ImGuiCol_TabHovered] = ImVec4(0.37f, 0.37f, 0.37f, 0.80f);
		colors[ImGuiCol_TabActive] = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
		colors[ImGuiCol_TabUnfocused] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.11f, 0.15f, 0.17f, 1.00f);
		colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.59f, 0.98f, 0.70f);
		colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
		colors[ImGuiCol_PlotLines] = ImVec4(0.73f, 0.29f, 0.29f, 1.00f);
		colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
		colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
		colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);
		colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
		colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
		colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
		colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		colors[ImGuiCol_TextSelectedBg] = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
		colors[ImGuiCol_DragDropTarget] = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
		colors[ImGuiCol_NavHighlight] = ImVec4(0.26f, 0.59f, 0.98f, 1.00f);
		colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
		colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
		colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.35f);
	}
	void Editor::HandleInput()
	{
		if (scene_focused && Input::GetInstance().IsKeyDown(EKeyCode::I)) gui->ToggleVisibility();
		if (scene_focused && Input::GetInstance().IsKeyDown(EKeyCode::G)) gizmo_enabled = !gizmo_enabled;
		if (gizmo_enabled && gui->IsVisible())
		{
			if (Input::GetInstance().IsKeyDown(EKeyCode::T)) gizmo_op = ImGuizmo::TRANSLATE;
			if (Input::GetInstance().IsKeyDown(EKeyCode::R)) gizmo_op = ImGuizmo::ROTATE;
			if (Input::GetInstance().IsKeyDown(EKeyCode::E)) gizmo_op = ImGuizmo::SCALE;
		}
		engine->camera->Enable(scene_focused);
	}
	void Editor::MenuBar()
	{
		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Load Model"))
				{
					nfdchar_t* file_path = NULL;
					const nfdchar_t* filter_list = "gltf";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string model_path = file_path;

						ModelParameters params{};
						params.model_path = model_path;
						std::string texture_path = GetParentPath(model_path);
						if (!texture_path.empty()) texture_path.append("/");

						params.textures_path = texture_path;
						engine->entity_loader->ImportModel_GLTF(params);
						free(file_path);
					}
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Windows"))
			{
				if (ImGui::MenuItem("Profiler", 0, window_flags[Flag_Profiler]))			 window_flags[Flag_Profiler] = !window_flags[Flag_Profiler];
				if (ImGui::MenuItem("Log", 0, window_flags[Flag_Log]))						 window_flags[Flag_Log] = !window_flags[Flag_Log];
				if (ImGui::MenuItem("Camera", 0, window_flags[Flag_Camera]))				 window_flags[Flag_Camera] = !window_flags[Flag_Camera];
				if (ImGui::MenuItem("Entities", 0, window_flags[Flag_Entities]))			 window_flags[Flag_Entities] = !window_flags[Flag_Entities];
				if (ImGui::MenuItem("Hot Reload", 0, window_flags[Flag_HotReload]))			 window_flags[Flag_HotReload] = !window_flags[Flag_HotReload];
				if (ImGui::MenuItem("Settings", 0, window_flags[Flag_Settings]))	 window_flags[Flag_Settings] = !window_flags[Flag_Settings];
				if (ImGui::MenuItem("Ray Tracing Debug", 0, window_flags[Flag_RTDebug]))	 window_flags[Flag_RTDebug] = !window_flags[Flag_RTDebug];
				if (ImGui::MenuItem("Add Entities", 0, window_flags[Flag_AddEntities]))		 window_flags[Flag_AddEntities] = !window_flags[Flag_AddEntities];

				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Help"))
			{
				ImGui::Text("Controls\n");
				ImGui::Text(
					"Move Camera with W, A, S, D, Q and E. Use Mouse for Rotating Camera. Use Mouse Scroll for Zoom In/Out.\n"
					"Press I to toggle between Cinema Mode and Editor Mode. (Scene Window has to be active) \n"
					"Press G to toggle Gizmo. (Scene Window has to be active) \n"
					"When Gizmo is enabled, use T, R and E to switch between Translation, Rotation and Scaling Mode.\n"
					"Left Click on entity to select it. Left click again on selected entity to unselect it.\n"
					"Right Click on empty area in Entities window to add entity. Right Click on selected entity to delete it.\n"
					"When placing decals, right click on focused Scene window to pick a point for a decal (it's used only for "
					"decals currently but that could change in the future)"
				);
				ImGui::Spacing();

				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}
	}

	void Editor::AddEntities()
	{
		if (!window_flags[Flag_AddEntities]) return;
		if (ImGui::Begin("Add Entities", &window_flags[Flag_AddEntities]))
		{
			if (ImGui::TreeNodeEx("Point Lights", 0))
			{
				ImGui::Text("For Easy Demonstration of Tiled Deferred Rendering");
				static int light_count_to_add = 1;
				ImGui::SliderInt("Light Count", &light_count_to_add, 1, 128);
				if (ImGui::Button("Create Random Lights"))
				{
					static RealRandomGenerator real(0.0f, 1.0f);

					for (int32 i = 0; i < light_count_to_add; ++i)
					{
						LightParameters light_params{};
						light_params.light_data.casts_shadows = false;
						light_params.light_data.color = DirectX::XMVectorSet(real() * 2, real() * 2, real() * 2, 1.0f);
						light_params.light_data.direction = DirectX::XMVectorSet(0.5f, -1.0f, 0.1f, 0.0f);
						light_params.light_data.position = DirectX::XMVectorSet(real() * 200 - 100, real() * 200.0f, real() * 200 - 100, 1.0f);
						light_params.light_data.type = ELightType::Point;
						light_params.mesh_type = ELightMesh::NoMesh;
						light_params.light_data.range = real() * 100.0f + 40.0f;
						light_params.light_data.active = true;
						light_params.light_data.volumetric = false;
						light_params.light_data.volumetric_strength = 1.0f;
						engine->entity_loader->LoadLight(light_params);
					}
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Ocean", 0))
			{
				static GridParameters ocean_params{};
				static int32 tile_count[2] = { 512, 512 };
				static float32 tile_size[2] = { 40.0f, 40.0f };
				static float32 texture_scale[2] = { 20.0f, 20.0f };

				ImGui::SliderInt2("Tile Count", tile_count, 32, 1024);
				ImGui::SliderFloat2("Tile Size", tile_size, 1.0, 100.0f);
				ImGui::SliderFloat2("Texture Scale", texture_scale, 0.1f, 10.0f);

				ocean_params.tile_count_x = tile_count[0];
				ocean_params.tile_count_z = tile_count[1];
				ocean_params.tile_size_x = tile_size[0];
				ocean_params.tile_size_z = tile_size[1];
				ocean_params.texture_scale_x = texture_scale[0];
				ocean_params.texture_scale_z = texture_scale[1];

				if (ImGui::Button("Load Ocean"))
				{
					OceanParameters params{};
					params.ocean_grid = std::move(ocean_params);
					engine->entity_loader->LoadOcean(params);
				}

				if (ImGui::Button("Clear"))
				{
					engine->reg.clear<Ocean>();
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Decals", 0))
			{
				static DecalParameters params{};
				static char NAME_BUFFER[128];
				ImGui::InputText("Name", NAME_BUFFER, sizeof(NAME_BUFFER));
				params.name = std::string(NAME_BUFFER);
				ImGui::PushID(6);
				if (ImGui::Button("Select Albedo Texture"))
				{
					nfdchar_t* file_path = NULL;
					nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string texture_path = file_path;
						params.albedo_texture_path = texture_path;
						free(file_path);
					}
				}
				ImGui::PopID();
				ImGui::Text(params.albedo_texture_path.c_str());

				ImGui::PushID(7);
				if (ImGui::Button("Select Normal Texture"))
				{
					nfdchar_t* file_path = NULL;
					nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::string texture_path = file_path;
						params.normal_texture_path = texture_path;
						free(file_path);
					}
				}

				ImGui::PopID();
				ImGui::Text(params.normal_texture_path.c_str());

				ImGui::DragFloat("Size", &params.size, 2.0f, 10.0f, 200.0f);
				ImGui::DragFloat("Rotation", &params.rotation, 1.0f, -180.0f, 180.0f);
				ImGui::Checkbox("Modify GBuffer Normals", &params.modify_gbuffer_normals);

				auto picking_data = engine->renderer->GetPickingData();
				ImGui::Text("Picked Position: %f %f %f", picking_data.position.x, picking_data.position.y, picking_data.position.z);
				ImGui::Text("Picked Normal: %f %f %f", picking_data.normal.x, picking_data.normal.y, picking_data.normal.z);
				if (ImGui::Button("Load Decal"))
				{
					params.position = picking_data.position;
					params.normal = picking_data.normal;
					params.rotation = XMConvertToRadians(params.rotation);

					engine->entity_loader->LoadDecal(params);
				}
				if (ImGui::Button("Clear Decals"))
				{
					for (auto e : engine->reg.view<Decal>()) engine->reg.destroy(e);
				}
				ImGui::TreePop();
				ImGui::Separator();
			}
			if (ImGui::TreeNodeEx("Particles", 0))
			{
				static EmitterParameters params{};
				static char NAME_BUFFER[128];
				ImGui::InputText("Name", NAME_BUFFER, sizeof(NAME_BUFFER));
				params.name = std::string(NAME_BUFFER);
				if (ImGui::Button("Select Texture"))
				{
					nfdchar_t* file_path = NULL;
					nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
					nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
					if (result == NFD_OKAY)
					{
						std::wstring texture_path = ToWideString(file_path);
						params.texture_path = texture_path;
						free(file_path);
					}
				}

				ImGui::Text(ToString(params.texture_path).c_str());
				ImGui::SliderFloat3("Position", params.position, -500.0f, 500.0f);
				ImGui::SliderFloat3("Velocity", params.velocity, -50.0f, 50.0f);
				ImGui::SliderFloat3("Position Variance", params.position_variance, -50.0f, 50.0f);
				ImGui::SliderFloat("Velocity Variance", &params.velocity_variance, -10.0f, 10.0f);
				ImGui::SliderFloat("Lifespan", &params.lifespan, 0.0f, 50.0f);
				ImGui::SliderFloat("Start Size", &params.start_size, 0.0f, 50.0f);
				ImGui::SliderFloat("End Size", &params.end_size, 0.0f, 10.0f);
				ImGui::SliderFloat("Mass", &params.mass, 0.0f, 10.0f);
				ImGui::SliderFloat("Particles Per Second", &params.particles_per_second, 1.0f, 1000.0f);
				ImGui::Checkbox("Alpha Blend", &params.blend);
				ImGui::Checkbox("Collisions", &params.collisions);
				ImGui::Checkbox("Sort", &params.sort);
				if (params.collisions) ImGui::SliderInt("Collision Thickness", &params.collision_thickness, 0, 40);

				if (ImGui::Button("Load Emitter"))
				{
					entt::entity e = engine->entity_loader->LoadEmitter(params);
					editor_events.particle_emitter_added.Broadcast(entt::to_integral(e));
				}
			}
		}
		ImGui::End();
	}
	void Editor::ListEntities()
	{
		if (!window_flags[Flag_Entities]) return;
		auto all_entities = engine->reg.view<Tag>();
		if (ImGui::Begin("Entities", &window_flags[Flag_Entities]))
		{
			std::vector<entt::entity> deleted_entities{};
			std::function<void(entt::entity, bool)> ShowEntity;
			ShowEntity = [&](entt::entity e, bool first_iteration)
			{
				Relationship* relationship = engine->reg.try_get<Relationship>(e);
				if (first_iteration && relationship && relationship->parent != entt::null) return;
				auto& tag = all_entities.get<Tag>(e);

				ImGuiTreeNodeFlags flags = ((selected_entity == e) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
				flags |= ImGuiTreeNodeFlags_SpanAvailWidth;
				bool opened = ImGui::TreeNodeEx(tag.name.c_str(), flags);

				if (ImGui::IsItemClicked())
				{
					if (e == selected_entity) selected_entity = entt::null;
					else selected_entity = e;
				}

				if (opened)
				{
					if (relationship)
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							ShowEntity(relationship->children[i], false);
						}
					}
					ImGui::TreePop();
				}
			};
			for (auto e : all_entities) ShowEntity(e, true);
		}
		ImGui::End();
	}
	void Editor::Properties()
	{
		if (!window_flags[Flag_Entities]) return;
		if (ImGui::Begin("Properties", &window_flags[Flag_Entities]))
		{
			if (selected_entity != entt::null)
			{
				auto tag = engine->reg.try_get<Tag>(selected_entity);
				if (tag)
				{
					char buffer[256];
					memset(buffer, 0, sizeof(buffer));
					std::strncpy(buffer, tag->name.c_str(), sizeof(buffer));
					if (ImGui::InputText("##Tag", buffer, sizeof(buffer)))
						tag->name = std::string(buffer);
				}

				auto light = engine->reg.try_get<Light>(selected_entity);
				if (light && ImGui::CollapsingHeader("Light"))
				{
					if (light->type == ELightType::Directional)	ImGui::Text("Directional Light");
					else if (light->type == ELightType::Spot)	ImGui::Text("Spot Light");
					else if (light->type == ELightType::Point)	ImGui::Text("Point Light");

					XMFLOAT4 light_color, light_direction, light_position;
					XMStoreFloat4(&light_color, light->color);
					XMStoreFloat4(&light_direction, light->direction);
					XMStoreFloat4(&light_position, light->position);

					float32 color[3] = { light_color.x, light_color.y, light_color.z };
					ImGui::ColorEdit3("Light Color", color);
					light->color = XMVectorSet(color[0], color[1], color[2], 1.0f);

					ImGui::SliderFloat("Light Energy", &light->energy, 0.0f, 50.0f);

					if (engine->reg.all_of<Material>(selected_entity))
					{
						auto& material = engine->reg.get<Material>(selected_entity);
						material.diffuse = XMFLOAT3(color[0], color[1], color[2]);
					}

					if (light->type == ELightType::Directional || light->type == ELightType::Spot)
					{
						float32 direction[3] = { light_direction.x, light_direction.y, light_direction.z };

						ImGui::SliderFloat3("Light direction", direction, -1.0f, 1.0f);

						light->direction = XMVectorSet(direction[0], direction[1], direction[2], 0.0f);

						if (light->type == ELightType::Directional)
						{
							light->position = XMVectorScale(-light->direction, 1e3);
						}
					}

					if (light->type == ELightType::Spot)
					{
						float32 inner_angle = XMConvertToDegrees(acos(light->inner_cosine))
							, outer_angle = XMConvertToDegrees(acos(light->outer_cosine));
						ImGui::SliderFloat("Inner Spot Angle", &inner_angle, 0.0f, 90.0f);
						ImGui::SliderFloat("Outer Spot Angle", &outer_angle, inner_angle, 90.0f);

						light->inner_cosine = cos(XMConvertToRadians(inner_angle));
						light->outer_cosine = cos(XMConvertToRadians(outer_angle));
					}

					if (light->type == ELightType::Point || light->type == ELightType::Spot)
					{
						float32 position[3] = { light_position.x, light_position.y, light_position.z };

						ImGui::SliderFloat3("Light position", position, -300.0f, 500.0f);

						light->position = XMVectorSet(position[0], position[1], position[2], 1.0f);

						ImGui::SliderFloat("Range", &light->range, 50.0f, 1000.0f);
					}

					if (engine->reg.all_of<Transform>(selected_entity))
					{
						auto& tr = engine->reg.get<Transform>(selected_entity);

						tr.current_transform = XMMatrixTranslationFromVector(light->position);
					}

					ImGui::Checkbox("Active", &light->active);

					if (light->type == ELightType::Directional)
					{
						const char* shadow_types[] = { "None", "Shadow Maps", "Ray Traced Shadows" };
						static int current_shadow_type = light->casts_shadows;
						const char* combo_label = shadow_types[current_shadow_type];
						if (ImGui::BeginCombo("Shadows Type", combo_label, 0))
						{
							for (int n = 0; n < IM_ARRAYSIZE(shadow_types); n++)
							{
								const bool is_selected = (current_shadow_type == n);
								if (ImGui::Selectable(shadow_types[n], is_selected)) current_shadow_type = n;
								if (is_selected) ImGui::SetItemDefaultFocus();
							}
							ImGui::EndCombo();
						}
						light->casts_shadows = (current_shadow_type == 1);
						light->ray_traced_shadows = (current_shadow_type == 2);
					}
					else
					{
						ImGui::Checkbox("Casts Shadows", &light->casts_shadows);
					}

					if (light->casts_shadows)
					{
						if (light->type == ELightType::Directional && light->casts_shadows)
						{
							bool use_cascades = static_cast<bool>(light->use_cascades);
							ImGui::Checkbox("Use Cascades", &use_cascades);
							light->use_cascades = use_cascades;
						}
						ImGui::Checkbox("Screen Space Contact Shadows", &light->sscs);
						if (light->sscs)
						{
							ImGui::SliderFloat("Thickness", &light->sscs_thickness, 0.0f, 1.0f);
							ImGui::SliderFloat("Max Ray Distance", &light->sscs_max_ray_distance, 0.0f, 0.3f);
							ImGui::SliderFloat("Max Depth Distance", &light->sscs_max_depth_distance, 0.0f, 500.0f);
						}
					}
					else if (light->ray_traced_shadows)
					{
						ImGui::Checkbox("Soft Shadows", &light->soft_rts);
						//add softness
					}

					ImGui::Checkbox("God Rays", &light->god_rays);
					if (light->god_rays)
					{
						ImGui::SliderFloat("God Rays decay", &light->godrays_decay, 0.0f, 1.0f);
						ImGui::SliderFloat("God Rays weight", &light->godrays_weight, 0.0f, 1.0f);
						ImGui::SliderFloat("God Rays density", &light->godrays_density, 0.1f, 2.0f);
						ImGui::SliderFloat("God Rays exposure", &light->godrays_exposure, 0.1f, 10.0f);
					}

					ImGui::Checkbox("Volumetric Lighting", &light->volumetric);
					if (light->volumetric)
					{
						ImGui::SliderFloat("Volumetric lighting Strength", &light->volumetric_strength, 0.0f, 5.0f);
					}

					ImGui::Checkbox("Lens Flare", &light->lens_flare);
				}

				auto material = engine->reg.try_get<Material>(selected_entity);
				if (material && ImGui::CollapsingHeader("Material"))
				{
					ID3D12Device5* device = engine->gfx->GetDevice();
					RingOnlineDescriptorAllocator* descriptor_allocator = gui->DescriptorAllocator();

					ImGui::Text("Albedo Texture");
					D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetTextureManager().GetSRV(material->albedo_texture);
					OffsetType descriptor_index = descriptor_allocator->Allocate();
					auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(0);
					if (ImGui::Button("Remove")) material->albedo_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->albedo_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Metallic-Roughness Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(material->metallic_roughness_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(1);
					if (ImGui::Button("Remove")) material->metallic_roughness_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->metallic_roughness_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Emissive Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(material->emissive_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(2);
					if (ImGui::Button("Remove")) material->emissive_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							material->emissive_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::ColorEdit3("Albedo Color", &material->diffuse.x);
					ImGui::SliderFloat("Albedo Factor", &material->albedo_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Metallic Factor", &material->metallic_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Roughness Factor", &material->roughness_factor, 0.0f, 1.0f);
					ImGui::SliderFloat("Emissive Factor", &material->emissive_factor, 0.0f, 32.0f);

					//add shader changing
					if (engine->reg.all_of<Forward>(selected_entity))
					{
						if (material->albedo_texture != INVALID_TEXTURE_HANDLE)
							material->pso = EPipelineState::Texture;
						else material->pso = EPipelineState::Solid;
					}
					else
					{
						material->pso = EPipelineState::GBufferPBR;
					}
				}

				auto transform = engine->reg.try_get<Transform>(selected_entity);
				if (transform && ImGui::CollapsingHeader("Transform"))
				{
					XMFLOAT4X4 tr;
					XMStoreFloat4x4(&tr, transform->current_transform);

					float translation[3], rotation[3], scale[3];
					ImGuizmo::DecomposeMatrixToComponents(tr.m[0], translation, rotation, scale);
					bool change = ImGui::InputFloat3("Translation", translation);
					change &= ImGui::InputFloat3("Rotation", rotation);
					change &= ImGui::InputFloat3("Scale", scale);
					ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, tr.m[0]);

					if (Emitter* emitter = engine->reg.try_get<Emitter>(selected_entity))
					{
						emitter->position = XMFLOAT4(translation[0], translation[1], translation[2], 1.0f);
					}

					if (AABB* aabb = engine->reg.try_get<AABB>(selected_entity))
					{
						aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, transform->current_transform));
						aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
						if(change) aabb_updates.push(aabb);
					}

					if (Relationship* relationship = engine->reg.try_get<Relationship>(selected_entity))
					{
						for (size_t i = 0; i < relationship->children_count; ++i)
						{
							entt::entity child = relationship->children[i];
							if (AABB* aabb = engine->reg.try_get<AABB>(child))
							{
								aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, transform->current_transform));
								aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
								if (change) aabb_updates.push(aabb);
							}
						}
					}
					transform->current_transform = DirectX::XMLoadFloat4x4(&tr);
				}

				auto emitter = engine->reg.try_get<Emitter>(selected_entity);
				if (emitter && ImGui::CollapsingHeader("Emitter"))
				{
					ID3D12Device5* device = engine->gfx->GetDevice();
					RingOnlineDescriptorAllocator* descriptor_allocator = gui->DescriptorAllocator();

					ImGui::Text("Particle Texture");
					D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetTextureManager().GetSRV(emitter->particle_texture);
					OffsetType descriptor_index = descriptor_allocator->Allocate();
					auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(3);
					if (ImGui::Button("Remove")) emitter->particle_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							emitter->particle_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					float32 pos[3] = { emitter->position.x, emitter->position.y, emitter->position.z },
						vel[3] = { emitter->velocity.x, emitter->velocity.y, emitter->velocity.z },
						pos_var[3] = { emitter->position_variance.x, emitter->position_variance.y, emitter->position_variance.z };

					ImGui::SliderFloat3("Position", pos, -500.0f, 500.0f);
					ImGui::SliderFloat3("Velocity", vel, -50.0f, 50.0f);
					ImGui::SliderFloat3("Position Variance", pos_var, -50.0f, 50.0f);
					emitter->position = DirectX::XMFLOAT4(pos[0], pos[1], pos[2], 1.0f);
					emitter->velocity = DirectX::XMFLOAT4(vel[0], vel[1], vel[2], 1.0f);
					emitter->position_variance = DirectX::XMFLOAT4(pos_var[0], pos_var[1], pos_var[2], 1.0f);

					if (transform)
					{
						XMFLOAT4X4 tr;
						XMStoreFloat4x4(&tr, transform->current_transform);
						float32 translation[3], rotation[3], scale[3];
						ImGuizmo::DecomposeMatrixToComponents(tr.m[0], translation, rotation, scale);
						ImGuizmo::RecomposeMatrixFromComponents(pos, rotation, scale, tr.m[0]);
						transform->current_transform = DirectX::XMLoadFloat4x4(&tr);
					}

					ImGui::SliderFloat("Velocity Variance", &emitter->velocity_variance, -10.0f, 10.0f);
					ImGui::SliderFloat("Lifespan", &emitter->particle_lifespan, 0.0f, 50.0f);
					ImGui::SliderFloat("Start Size", &emitter->start_size, 0.0f, 50.0f);
					ImGui::SliderFloat("End Size", &emitter->end_size, 0.0f, 10.0f);
					ImGui::SliderFloat("Mass", &emitter->mass, 0.0f, 10.0f);
					ImGui::SliderFloat("Particles Per Second", &emitter->particles_per_second, 1.0f, 1000.0f);

					ImGui::Checkbox("Alpha Blend", &emitter->alpha_blended);
					ImGui::Checkbox("Collisions", &emitter->collisions_enabled);
					if (emitter->collisions_enabled) ImGui::SliderInt("Collision Thickness", &emitter->collision_thickness, 0, 40);

					ImGui::Checkbox("Sort", &emitter->sort);
					ImGui::Checkbox("Pause", &emitter->pause);
					if (ImGui::Button("Reset")) emitter->reset_emitter = true;
				}

				auto decal = engine->reg.try_get<Decal>(selected_entity);
				if (decal && ImGui::CollapsingHeader("Decal"))
				{
					ID3D12Device5* device = engine->gfx->GetDevice();
					RingOnlineDescriptorAllocator* descriptor_allocator = gui->DescriptorAllocator();

					ImGui::Text("Decal Albedo Texture");
					D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetTextureManager().GetSRV(decal->albedo_decal_texture);
					OffsetType descriptor_index = descriptor_allocator->Allocate();
					auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(4);
					if (ImGui::Button("Remove")) decal->albedo_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->albedo_decal_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();

					ImGui::Text("Decal Normal Texture");
					tex_handle = engine->renderer->GetTextureManager().GetSRV(decal->normal_decal_texture);
					descriptor_index = descriptor_allocator->Allocate();
					dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
					device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
					ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr,
						ImVec2(48.0f, 48.0f));

					ImGui::PushID(5);
					if (ImGui::Button("Remove")) decal->normal_decal_texture = INVALID_TEXTURE_HANDLE;
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							decal->normal_decal_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
					ImGui::PopID();
					ImGui::Checkbox("Modify GBuffer Normals", &decal->modify_gbuffer_normals);
				}

				auto skybox = engine->reg.try_get<Skybox>(selected_entity);
				if (skybox && ImGui::CollapsingHeader("Skybox"))
				{
					ImGui::Checkbox("Active", &skybox->active);
					if (ImGui::Button("Select"))
					{
						nfdchar_t* file_path = NULL;
						nfdchar_t const* filter_list = "jpg,jpeg,tga,dds,png";
						nfdresult_t result = NFD_OpenDialog(filter_list, NULL, &file_path);
						if (result == NFD_OKAY)
						{
							std::wstring texture_path = ToWideString(file_path);
							skybox->cubemap_texture = engine->renderer->GetTextureManager().LoadTexture(texture_path);
							free(file_path);
						}
					}
				}

				auto forward = engine->reg.try_get<Forward>(selected_entity);
				if (forward)
				{
					if (ImGui::CollapsingHeader("Forward")) ImGui::Checkbox("Transparent", &forward->transparent);
				}

				if (AABB* aabb = engine->reg.try_get<AABB>(selected_entity))
				{
					aabb->draw_aabb = true;
				}
			}
		}
		ImGui::End();
	}
	void Editor::Camera()
	{
		if (!window_flags[Flag_Camera]) return;
		
		auto& camera = *engine->camera;
		if (ImGui::Begin("Camera", &window_flags[Flag_Camera]))
		{
			XMFLOAT3 cam_pos;
			XMStoreFloat3(&cam_pos, camera.Position());
			float32 pos[3] = { cam_pos.x , cam_pos.y, cam_pos.z };
			ImGui::SliderFloat3("Position", pos, 0.0f, 2000.0f);
			camera.SetPosition(DirectX::XMFLOAT3(pos));
			float32 near_plane = camera.Near(), far_plane = camera.Far();
			float32 _fov = camera.Fov(), _ar = camera.AspectRatio();
			ImGui::SliderFloat("Near Plane", &near_plane, 0.0f, 2.0f);
			ImGui::SliderFloat("Far Plane", &far_plane, 10.0f, 3000.0f);
			ImGui::SliderFloat("FOV", &_fov, 0.01f, 1.5707f);
			camera.SetNearAndFar(near_plane, far_plane);
			camera.SetFov(_fov);
		}
		ImGui::End();
	}
	void Editor::Scene()
	{
		ImGui::Begin("Scene");
		{
			auto device = engine->gfx->GetDevice();
			auto descriptor_allocator = gui->DescriptorAllocator();

			ImVec2 v_min = ImGui::GetWindowContentRegionMin();
			ImVec2 v_max = ImGui::GetWindowContentRegionMax();
			v_min.x += ImGui::GetWindowPos().x;
			v_min.y += ImGui::GetWindowPos().y;
			v_max.x += ImGui::GetWindowPos().x;
			v_max.y += ImGui::GetWindowPos().y;
			ImVec2 size(v_max.x - v_min.x, v_max.y - v_min.y);

			D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetFinalTexture()->GetSRV();
			OffsetType descriptor_index = descriptor_allocator->Allocate();
			auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
			device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
			ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);

			scene_focused = ImGui::IsWindowFocused();

			ImVec2 mouse_pos = ImGui::GetMousePos();
			viewport_data.mouse_position_x = mouse_pos.x;
			viewport_data.mouse_position_y = mouse_pos.y;
			viewport_data.scene_viewport_focused = scene_focused;
			viewport_data.scene_viewport_pos_x = v_min.x;
			viewport_data.scene_viewport_pos_y = v_min.y;
			viewport_data.scene_viewport_size_x = size.x;
			viewport_data.scene_viewport_size_y = size.y;
		}

		if (selected_entity != entt::null && engine->reg.all_of<Transform>(selected_entity) && gizmo_enabled)
		{
			ImGuizmo::SetDrawlist();

			ImVec2 window_size = ImGui::GetWindowSize();
			ImVec2 window_pos = ImGui::GetWindowPos();

			ImGuizmo::SetRect(window_pos.x, window_pos.y,
				window_size.x, window_size.y);

			auto& camera = *engine->camera;

			auto camera_view = camera.View();
			auto camera_proj = camera.Proj();

			DirectX::XMFLOAT4X4 view, projection;

			DirectX::XMStoreFloat4x4(&view, camera_view);
			DirectX::XMStoreFloat4x4(&projection, camera_proj);

			auto& entity_transform = engine->reg.get<Transform>(selected_entity);

			DirectX::XMFLOAT4X4 tr;
			DirectX::XMStoreFloat4x4(&tr, entity_transform.current_transform);

			bool change = ImGuizmo::Manipulate(view.m[0], projection.m[0], gizmo_op, ImGuizmo::LOCAL,
				tr.m[0]);

			if (ImGuizmo::IsUsing())
			{
				AABB* aabb = engine->reg.try_get<AABB>(selected_entity);
				if (aabb)
				{
					aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, entity_transform.current_transform));
					aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
					if(change) aabb_updates.push(aabb);
				}

				if (Relationship* relationship = engine->reg.try_get<Relationship>(selected_entity))
				{
					for (size_t i = 0; i < relationship->children_count; ++i)
					{
						entt::entity child = relationship->children[i];
						if (AABB* aabb = engine->reg.try_get<AABB>(child))
						{
							aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMMatrixInverse(nullptr, entity_transform.current_transform));
							aabb->bounding_box.Transform(aabb->bounding_box, DirectX::XMLoadFloat4x4(&tr));
							if (change) aabb_updates.push(aabb);
						}
					}
				}
				entity_transform.current_transform = DirectX::XMLoadFloat4x4(&tr);
			}
		}

		ImGui::End();
	}
	void Editor::Log()
	{
		if (!window_flags[Flag_Log]) return;
		if(ImGui::Begin("Log", &window_flags[Flag_Log]))
		{
			editor_log->Draw("Log");
		}
		ImGui::End();
	}
	void Editor::Settings()
	{
		if (!window_flags[Flag_Settings]) return;
		if (ImGui::Begin("Settings", &window_flags[Flag_Settings]))
		{
			static const char* render_path_types[] = { "Regular", "Tiled", "Clustered" };
			static int current_render_path_type = 0;
			const char* render_path_combo_label = render_path_types[current_render_path_type];
			if (ImGui::BeginCombo("Render Path", render_path_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(render_path_types); n++)
				{
					const bool is_selected = (current_render_path_type == n);
					if (ImGui::Selectable(render_path_types[n], is_selected)) current_render_path_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			renderer_settings.use_tiled_deferred = (current_render_path_type == 1);
			renderer_settings.use_clustered_deferred = (current_render_path_type == 2);

			static const char* ao_types[] = { "None", "SSAO", "HBAO", "RTAO" };
			static int current_ao_type = 0;
			const char* ao_combo_label = ao_types[current_ao_type];
			if (ImGui::BeginCombo("Ambient Occlusion", ao_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(ao_types); n++)
				{
					const bool is_selected = (current_ao_type == n);
					if (ImGui::Selectable(ao_types[n], is_selected)) current_ao_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			renderer_settings.postprocess.ambient_occlusion = static_cast<EAmbientOcclusion>(current_ao_type);
			
			static const char* reflection_types[] = { "None", "SSR", "RTR" };
			static int current_reflection_type = (int)renderer_settings.postprocess.reflections;
			const char* reflection_combo_label = reflection_types[current_reflection_type];
			if (ImGui::BeginCombo("Reflections", reflection_combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(reflection_types); n++)
				{
					const bool is_selected = (current_reflection_type == n);
					if (ImGui::Selectable(reflection_types[n], is_selected)) current_reflection_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			renderer_settings.postprocess.reflections = static_cast<EReflections>(current_reflection_type);
			ImGui::Checkbox("Automatic Exposure", &renderer_settings.postprocess.automatic_exposure);
			ImGui::Checkbox("Volumetric Clouds", &renderer_settings.postprocess.clouds);
			ImGui::Checkbox("DoF", &renderer_settings.postprocess.dof);
			if (renderer_settings.postprocess.dof)
			{
				ImGui::Checkbox("Bokeh", &renderer_settings.postprocess.bokeh);
			}
			ImGui::Checkbox("Bloom", &renderer_settings.postprocess.bloom);
			ImGui::Checkbox("Motion Blur", &renderer_settings.postprocess.motion_blur);
			ImGui::Checkbox("Fog", &renderer_settings.postprocess.fog);
			if (ImGui::TreeNode("Anti-Aliasing"))
			{
				static bool fxaa = false, taa = false;
				ImGui::Checkbox("FXAA", &fxaa);
				ImGui::Checkbox("TAA", &taa);
				if (fxaa) renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing | AntiAliasing_FXAA);
				else renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing & (~AntiAliasing_FXAA));

				if (taa) renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing | AntiAliasing_TAA);
				else renderer_settings.postprocess.anti_aliasing = static_cast<EAntiAliasing>(renderer_settings.postprocess.anti_aliasing & (~AntiAliasing_TAA));

				ImGui::TreePop();
			}

			for (auto&& command : commands) command.callback();
			commands.clear();

			if (ImGui::TreeNode("Misc"))
			{
				ImGui::ColorEdit3("Ambient Color", renderer_settings.ambient_color);
				ImGui::Checkbox("IBL", &renderer_settings.ibl);
				if (renderer_settings.ibl)
				{
					renderer_settings.ibl = false;
					ADRIA_LOG(INFO, "IBL is currently broken!");
				}
				ImGui::TreePop();
			}
		}
		ImGui::End();
	}
	void Editor::Profiling()
	{
		if (!window_flags[Flag_Profiler]) return;
		if (ImGui::Begin("Profiling", &window_flags[Flag_Profiler]))
		{
			ImGuiIO io = ImGui::GetIO();
			static bool enable_profiling = false;
			ImGui::Checkbox("Enable Profiling", &enable_profiling);
			if (enable_profiling)
			{
				static ProfilerState state;
				if (ImGui::CollapsingHeader("Profiler Settings", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Profile GBuffer Pass", &profiler_settings.profile_gbuffer_pass);
					ImGui::Checkbox("Profile Decal Pass", &profiler_settings.profile_decal_pass);
					ImGui::Checkbox("Profile Deferred Pass", &profiler_settings.profile_deferred_pass);
					ImGui::Checkbox("Profile Forward Pass", &profiler_settings.profile_forward_pass);
					ImGui::Checkbox("Profile Particles Pass", &profiler_settings.profile_particles_pass);
					ImGui::Checkbox("Profile Postprocessing", &profiler_settings.profile_postprocessing);
				}
				engine->renderer->SetProfilerSettings(profiler_settings);

				static constexpr uint64 NUM_FRAMES = 128;
				static float32 FRAME_TIME_ARRAY[NUM_FRAMES] = { 0 };
				static float32 RECENT_HIGHEST_FRAME_TIME = 0.0f;
				static constexpr int32 FRAME_TIME_GRAPH_MAX_FPS[] = { 800, 240, 120, 90, 65, 45, 30, 15, 10, 5, 4, 3, 2, 1 };
				static float32 FRAME_TIME_GRAPH_MAX_VALUES[ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS)] = { 0 };
				for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_FPS); ++i) { FRAME_TIME_GRAPH_MAX_VALUES[i] = 1000.f / FRAME_TIME_GRAPH_MAX_FPS[i]; }

				std::vector<Timestamp> time_stamps = engine->renderer->GetProfilerResults();
				FRAME_TIME_ARRAY[NUM_FRAMES - 1] = 1000.0f / io.Framerate;
				for (uint32 i = 0; i < NUM_FRAMES - 1; i++) FRAME_TIME_ARRAY[i] = FRAME_TIME_ARRAY[i + 1];
				RECENT_HIGHEST_FRAME_TIME = std::max(RECENT_HIGHEST_FRAME_TIME, FRAME_TIME_ARRAY[NUM_FRAMES - 1]);
				float32 frame_time_ms = FRAME_TIME_ARRAY[NUM_FRAMES - 1];
				const int32 fps = static_cast<int32>(1000.0f / frame_time_ms);

				ImGui::Text("FPS        : %d (%.2f ms)", fps, frame_time_ms);
				if (ImGui::CollapsingHeader("Timings", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Checkbox("Show Avg/Min/Max", &state.show_average);
					ImGui::Spacing();

					uint64 i_max = 0;
					for (uint64 i = 0; i < ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES); ++i)
					{
						if (RECENT_HIGHEST_FRAME_TIME < FRAME_TIME_GRAPH_MAX_VALUES[i]) // FRAME_TIME_GRAPH_MAX_VALUES are in increasing order
						{
							i_max = std::min(ARRAYSIZE(FRAME_TIME_GRAPH_MAX_VALUES) - 1, i + 1);
							break;
						}
					}
					ImGui::PlotLines("", FRAME_TIME_ARRAY, NUM_FRAMES, 0, "GPU frame time (ms)", 0.0f, FRAME_TIME_GRAPH_MAX_VALUES[i_max], ImVec2(0, 80));

					constexpr uint32_t avg_timestamp_update_interval = 1000;
					static auto MillisecondsNow = []()
					{
						static LARGE_INTEGER s_frequency;
						static BOOL s_use_qpc = QueryPerformanceFrequency(&s_frequency);
						double milliseconds = 0;
						if (s_use_qpc)
						{
							LARGE_INTEGER now;
							QueryPerformanceCounter(&now);
							milliseconds = double(1000.0 * now.QuadPart) / s_frequency.QuadPart;
						}
						else milliseconds = double(GetTickCount64());
						return milliseconds;
					};
					const double current_time = MillisecondsNow();

					bool reset_accumulating_state = false;
					if ((state.accumulating_frame_count > 1) &&
						((current_time - state.last_reset_time) > avg_timestamp_update_interval))
					{
						std::swap(state.displayed_timestamps, state.accumulating_timestamps);
						for (uint32_t i = 0; i < state.displayed_timestamps.size(); i++)
						{
							state.displayed_timestamps[i].sum /= state.accumulating_frame_count;
						}
						reset_accumulating_state = true;
					}

					reset_accumulating_state |= (state.accumulating_timestamps.size() != time_stamps.size());
					if (reset_accumulating_state)
					{
						state.accumulating_timestamps.resize(0);
						state.accumulating_timestamps.resize(time_stamps.size());
						state.last_reset_time = current_time;
						state.accumulating_frame_count = 0;
					}

					for (uint64 i = 0; i < time_stamps.size(); i++)
					{
						float32 value = time_stamps[i].time_in_ms;
						char const* pStrUnit = "ms";
						ImGui::Text("%-18s: %7.2f %s", time_stamps[i].name.c_str(), value, pStrUnit);
						if (state.show_average)
						{
							if (state.displayed_timestamps.size() == time_stamps.size())
							{
								ImGui::SameLine();
								ImGui::Text("  avg: %7.2f %s", state.displayed_timestamps[i].sum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  min: %7.2f %s", state.displayed_timestamps[i].minimum, pStrUnit);
								ImGui::SameLine();
								ImGui::Text("  max: %7.2f %s", state.displayed_timestamps[i].maximum, pStrUnit);
							}

							ProfilerState::AccumulatedTimeStamp* pAccumulatingTimeStamp = &state.accumulating_timestamps[i];
							pAccumulatingTimeStamp->sum += time_stamps[i].time_in_ms;
							pAccumulatingTimeStamp->minimum = std::min<float>(pAccumulatingTimeStamp->minimum, time_stamps[i].time_in_ms);
							pAccumulatingTimeStamp->maximum = std::max<float>(pAccumulatingTimeStamp->maximum, time_stamps[i].time_in_ms);
						}
					}
					state.accumulating_frame_count++;
				}
			}
			else
			{
				engine->renderer->SetProfilerSettings(NO_PROFILING);
			}
			static bool display_vram_usage = false;
			ImGui::Checkbox("Display VRAM Usage", &display_vram_usage);
			if (display_vram_usage)
			{
				GPUMemoryUsage vram = engine->gfx->GetMemoryUsage();
				float const ratio = vram.usage * 1.0f / vram.budget;
				std::string vram_display_string = "VRAM usage: " + std::to_string(vram.usage / 1024 / 1024) + "MB / " + std::to_string(vram.budget / 1024 / 1024) + "MB\n";
				if (ratio >= 0.9f && ratio <= 1.0f) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 0, 255));
				else if (ratio > 1.0f) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
				else ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
				ImGui::TextWrapped(vram_display_string.c_str());
				ImGui::PopStyleColor();
			}
		}
		ImGui::End();
	}
	void Editor::ShaderHotReload()
	{
		if (!window_flags[Flag_HotReload]) return;
		if (ImGui::Begin("Shader Hot Reload", &window_flags[Flag_HotReload]))
		{
			if (ImGui::Button("Compile Changed Shaders"))
			{
				reload_shaders = true;
			}
		}
		ImGui::End();
	}
	void Editor::RayTracingDebug()
	{
#ifdef _DEBUG

		if (!window_flags[Flag_RTDebug]) return;

		auto device = engine->gfx->GetDevice();
		auto descriptor_allocator = gui->DescriptorAllocator();
		ImVec2 v_min = ImGui::GetWindowContentRegionMin();
		ImVec2 v_max = ImGui::GetWindowContentRegionMax();
		v_min.x += ImGui::GetWindowPos().x;
		v_min.y += ImGui::GetWindowPos().y;
		v_max.x += ImGui::GetWindowPos().x;
		v_max.y += ImGui::GetWindowPos().y;
		ImVec2 size(v_max.x - v_min.x, v_max.y - v_min.y);

		if(ImGui::Begin("Ray Tracing Debug", &window_flags[Flag_RTDebug]))
		{
			static const char* rt_types[] = { "Shadows", "Ambient Occlusion", "Reflections" };
			static int current_rt_type = 0;
			const char* combo_label = rt_types[current_rt_type];
			if (ImGui::BeginCombo("RT Texture Type", combo_label, 0))
			{
				for (int n = 0; n < IM_ARRAYSIZE(rt_types); n++)
				{
					const bool is_selected = (current_rt_type == n);
					if (ImGui::Selectable(rt_types[n], is_selected)) current_rt_type = n;
					if (is_selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (current_rt_type == 0)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetRTSDebugTexture()->GetSRV();
				OffsetType descriptor_index = descriptor_allocator->Allocate();
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);
				ImGui::Text("Ray Tracing Shadows Image");
			}
			else if (current_rt_type == 1)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetRTAODebugTexture()->GetSRV();
				OffsetType descriptor_index = descriptor_allocator->Allocate();
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);
				ImGui::Text("Ray Tracing AO Image");
			}
			else if (current_rt_type == 2)
			{
				D3D12_CPU_DESCRIPTOR_HANDLE tex_handle = engine->renderer->GetRTRDebugTexture()->GetSRV();
				OffsetType descriptor_index = descriptor_allocator->Allocate();
				auto dst_descriptor = descriptor_allocator->GetHandle(descriptor_index);
				device->CopyDescriptorsSimple(1, dst_descriptor, tex_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				ImGui::Image((ImTextureID)static_cast<D3D12_GPU_DESCRIPTOR_HANDLE>(dst_descriptor).ptr, size);
				ImGui::Text("Ray Tracing Reflections Image");
			}

		}
		ImGui::End();
#endif
	}
}
