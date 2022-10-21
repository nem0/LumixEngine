#pragma once

#include "editor/render_interface.h"
#include "editor/studio_app.h"
#include "engine/engine.h"
#include "engine/math.h"
#include "engine/os.h"
#include "renderer/renderer.h"
#include <imgui/imgui.h>

namespace Lumix {

struct ImGuiCanvas {
	ImGuiCanvas(StudioApp& app) : m_app(app) {}

	~ImGuiCanvas() {
		if (m_rt) {	
			Renderer* renderer = (Renderer*)m_app.getEngine().getPluginManager().getPlugin("renderer");
			renderer->destroy(m_rt);
		}

		if (m_ctx) ImGui::DestroyContext(m_ctx);
	}

	void begin() {
		ImVec2 size = ImGui::GetContentRegionAvail();
		const bool is_resized = size.x != m_size.x || size.y != m_size.y;
		if (!m_rt || is_resized) {
			m_size = size;
			Renderer* renderer = (Renderer*)m_app.getEngine().getPluginManager().getPlugin("renderer");
			Renderer::MemRef mem;
			if (m_rt) renderer->destroy(m_rt);
			m_rt = renderer->createTexture((u32)m_size.x, (u32)m_size.y, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::RENDER_TARGET | gpu::TextureFlags::SRGB, mem, "imgui_canvas");
		}
		m_origin = ImGui::GetCursorScreenPos();
		m_original_ctx = ImGui::GetCurrentContext();
		if (!m_ctx) m_ctx = ImGui::CreateContext(ImGui::GetIO().Fonts);
		ImGui::SetCurrentContext(m_ctx);

		const os::Event* events = m_app.getEvents();
		ImGuiIO& io = ImGui::GetIO();
		for (i32 i = 0, c = m_app.getEventsCount(); i < c; ++i) {
			switch (events[i].type) {
				case os::Event::Type::CHAR: {
					char tmp[5] = {};
					memcpy(tmp, &events[i].text_input.utf8, sizeof(events[i].text_input.utf8));
					io.AddInputCharactersUTF8(tmp);
					break;
				}
				case os::Event::Type::KEY: {
					const ImGuiKey key = m_app.getImGuiKey((int)events[i].key.keycode);
					if (key != ImGuiKey_None) io.AddKeyEvent(key, events[i].key.down);
					break;
				}
				case os::Event::Type::MOUSE_BUTTON: {
					// TODO check if we should handle input, see studioapp how it's done there
					io.AddMouseButtonEvent((int)events[i].mouse_button.button, events[i].mouse_button.down);
					break;
				}
				case os::Event::Type::MOUSE_MOVE: {
					const os::Point cp = os::getMouseScreenPos();
					io.AddMousePosEvent((cp.x - m_origin.x) / m_scale.x, (cp.y - m_origin.y) / m_scale.y);
					break;
				}
			}
		}

		ImGui::GetIO().DisplaySize = m_size / m_scale;
		ImGui::NewFrame();

		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(m_size / m_scale);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::Begin("imgui_canvas", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
		ImGui::PopStyleVar();
	}

	void end() {
		ImGui::End();
		ImGui::Render();
		
		m_app.getRenderInterface()->renderImGuiCanvas(m_rt, Vec2(m_size.x, m_size.y), ImGui::GetDrawData(), m_scale);
		
		ImGui::SetCurrentContext(m_original_ctx);
		m_original_ctx = nullptr;
		ImGui::SetCursorScreenPos(m_origin);

		if (gpu::isOriginBottomLeft()) {
			ImGui::Image(m_rt, m_size, ImVec2(0, 1), ImVec2(1, 0));
		}
		else {
			ImGui::Image(m_rt, m_size);
		}

		if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel) {
			m_scale.x += ImGui::GetIO().MouseWheel / 20;
			m_scale.x = clamp(m_scale.x, 0.1f, 10.f);
			m_scale.y = m_scale.x;
		}
	}

	StudioApp& m_app;
	ImVec2 m_origin;
	ImVec2 m_size = ImVec2(0, 0);
	gpu::TextureHandle m_rt = gpu::INVALID_TEXTURE;
	ImVec2 m_scale = ImVec2(1, 1);
	ImGuiContext* m_ctx = nullptr;
	ImGuiContext* m_original_ctx = nullptr;
};

} // namespace Lumix