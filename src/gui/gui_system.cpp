#include "gui_system.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/iallocator.h"
#include "engine/input_system.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/plugin_manager.h"
#include "engine/resource_manager.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/pipeline.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include <bgfx/bgfx.h>
#include <imgui/imgui.h>


namespace Lumix
{


struct GUISystemImpl;


static const ResourceType MATERIAL_TYPE("material");
static const ResourceType TEXTURE_TYPE("texture");


struct GUISystemImpl LUMIX_FINAL : public GUISystem
{
	GUISystemImpl(Engine& engine)
		: m_engine(engine)
		, m_interface(nullptr)
	{
		m_context = ImGui::CreateContext();
		m_original_context = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();
		
		m_font = io.Fonts->AddFontFromFileTTF("ui/fonts/VeraMono.ttf", 20);
		u8* pixels;
		int w, h;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		auto* material_manager = m_engine.getResourceManager().get(MATERIAL_TYPE);
		auto* resource = material_manager->load(Path("pipelines/imgui/imgui.mat"));
		m_material = static_cast<Material*>(resource);

		auto* old_texture = m_material->getTexture(0);
		Texture* texture = LUMIX_NEW(m_engine.getAllocator(), Texture)(
			Path("font"), *m_engine.getResourceManager().get(TEXTURE_TYPE), m_engine.getAllocator());

		texture->create(w, h, pixels);
		m_material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), old_texture);
		}

		io.DisplaySize.x = 640;
		io.DisplaySize.y = 480;
		ImGui::NewFrame();
		ImGui::SetCurrentContext(m_original_context);

		registerLuaAPI();
	}


	~GUISystemImpl()
	{
		Texture* texture = m_material->getTexture(0);
		if (texture)
		{
			m_material->setTexture(0, nullptr);
			texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), texture);
		}

		m_material->getResourceManager().unload(*m_material);

		ImGui::DestroyContext(m_context);
	}


	void registerLuaAPI()
	{
		lua_State* L = m_engine.getState();
		
		#define REGISTER_FUNCTION(name) \
			do {\
				auto f = &LuaWrapper::wrapMethod<GUISystemImpl, decltype(&GUISystemImpl::name), \
					&GUISystemImpl::name>; \
				LuaWrapper::createSystemFunction(L, "Gui", #name, f); \
			} while(false) \

		REGISTER_FUNCTION(beginGUI);
		REGISTER_FUNCTION(endGUI);
		REGISTER_FUNCTION(enableCursor);
		REGISTER_FUNCTION(getMouseX);
		REGISTER_FUNCTION(getMouseY);
		REGISTER_FUNCTION(isMouseClicked);

		LuaWrapper::createSystemVariable(L, "Gui", "instance", this);

		#undef REGISTER_FUNCTION
	}


	void enableCursor(bool enable)
	{
		if (m_interface) m_interface->enableCursor(enable);
	}


	void setInterface(Interface* interface) override
	{
		m_interface = interface;
		
		if (!m_interface) return;

		auto* pipeline = m_interface->getPipeline();
		pipeline->addCustomCommandHandler("renderIngameGUI")
			.callback.bind<GUISystemImpl, &GUISystemImpl::pipelineCallback>(this);
	}


	void setGUIProjection()
	{
		Pipeline* pipeline = m_interface->getPipeline();
		Vec2 size((float)pipeline->getWidth(), (float)pipeline->getHeight());
		Matrix ortho;
		ortho.setOrtho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f, bgfx::getCaps()->homogeneousDepth);
		pipeline->setViewProjection(ortho, (int)size.x, (int)size.y);
	}


	void drawGUICmdList(ImDrawList* cmd_list)
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		if (!renderer) return;

		Pipeline* pipeline = m_interface->getPipeline();
		int num_indices = cmd_list->IdxBuffer.size();
		int num_vertices = cmd_list->VtxBuffer.size();
		auto& decl = renderer->getBasic2DVertexDecl();
		bgfx::TransientVertexBuffer vertex_buffer;
		bgfx::TransientIndexBuffer index_buffer;
		if (bgfx::getAvailTransientIndexBuffer(num_indices) < (u32)num_indices) return;
		if (bgfx::getAvailTransientVertexBuffer(num_vertices, decl) < (u32)num_vertices) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &cmd_list->VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &cmd_list->IdxBuffer[0], num_indices * sizeof(u16));

		u32 elem_offset = 0;
		const ImDrawCmd* pcmd_begin = cmd_list->CmdBuffer.begin();
		const ImDrawCmd* pcmd_end = cmd_list->CmdBuffer.end();
		for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if (pcmd->UserCallback)
			{
				pcmd->UserCallback(cmd_list, pcmd);
				elem_offset += pcmd->ElemCount;
				continue;
			}

			if (0 == pcmd->ElemCount) continue;

			pipeline->setScissor(u16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

			auto material = m_material;
			const auto& texture_id =
				pcmd->TextureId ? *(bgfx::TextureHandle*)pcmd->TextureId : material->getTexture(0)->handle;
			auto texture_uniform = material->getShader()->m_texture_slots[0].uniform_handle;
			pipeline->setTexture(0, texture_id, texture_uniform);
			pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				material->getRenderStates(),
				material->getShaderInstance());

			elem_offset += pcmd->ElemCount;
		}
	}


	void pipelineCallback()
	{
		if (!m_interface) return;

		m_original_context = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::Render();
		ImDrawData* draw_data = ImGui::GetDrawData();
		Pipeline* pipeline = m_interface->getPipeline();
		ImGui::GetIO().DisplaySize.x = (float)pipeline->getWidth();
		ImGui::GetIO().DisplaySize.y = (float)pipeline->getHeight();
		
		if (!pipeline->isReady()) return;

		setGUIProjection();

		for (int i = 0; i < draw_data->CmdListsCount; ++i)
		{
			ImDrawList* cmd_list = draw_data->CmdLists[i];
			drawGUICmdList(cmd_list);
		}

		ImGui::NewFrame();
		ImGui::SetCurrentContext(m_original_context);
	}


	void beginGUI() override
	{
		m_original_context = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::PushFont(m_font);
	}


	void endGUI() override
	{
		ImGui::PopFont();
		ImGui::SetCurrentContext(m_original_context);
	}


	float getMouseX() const
	{
		return ImGui::GetIO().MousePos.x - m_interface->getPos().x;
	}


	bool isMouseClicked(int button)
	{
		return ImGui::IsMouseClicked(button);
	}


	float getMouseY() const
	{
		return ImGui::GetIO().MousePos.x - m_interface->getPos().x;
	}


	void update(float time_delta) override
	{
		beginGUI();
		InputSystem& input_system = m_engine.getInputSystem();
		Vec2 mouse_pos = input_system.getCursorPosition() - m_interface->getPos();
		auto& io = ImGui::GetIO();
		io.MousePos = ImVec2(mouse_pos.x, mouse_pos.y);
		
		const InputSystem::Event* events = input_system.getEvents();
		for (int i = 0, c = input_system.getEventsCount(); i < c; ++i)
		{
			if (events[i].type == InputSystem::Event::BUTTON && events[i].device->type == InputSystem::Device::MOUSE && events[i].data.button.key_id == 1)
			{
				io.MouseDown[0] = events[i].data.button.state == InputSystem::ButtonEvent::DOWN;
			}
		}
		endGUI();
	}


	void stopGame() override {}


	const char* getName() const override { return "gui"; }


	Engine& m_engine;
	Interface* m_interface;
	ImGuiContext* m_context;
	ImGuiContext* m_original_context;
	ImFont* m_font;
	Material* m_material;
};


LUMIX_PLUGIN_ENTRY(gui)
{
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
