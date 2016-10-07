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
		
		m_font = io.Fonts->AddFontFromFileTTF("bin/VeraMono.ttf", 20);
		uint8* pixels;
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

		engine.getInputSystem().eventListener().bind<GUISystemImpl, &GUISystemImpl::onInputEvent>(this);
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

		m_engine.getInputSystem().eventListener().unbind<GUISystemImpl, &GUISystemImpl::onInputEvent>(this);
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
		Vec2 size = m_interface->getSize();
		Matrix ortho;
		bool is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
			bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
		ortho.setOrtho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f, is_opengl);
		Pipeline* pipeline = m_interface->getPipeline();
		pipeline->setViewport(0, 0, (int)size.x, (int)size.y);
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
		if (!bgfx::checkAvailTransientBuffers(num_vertices, decl, num_indices)) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &cmd_list->VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &cmd_list->IdxBuffer[0], num_indices * sizeof(uint16));

		uint32 elem_offset = 0;
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

			pipeline->setScissor(uint16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				uint16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

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
		if (m_interface)
		{
			ImGui::GetIO().DisplaySize.x = m_interface->getSize().x;
			ImGui::GetIO().DisplaySize.y = m_interface->getSize().y;
		}
		Pipeline* pipeline = m_interface->getPipeline();
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


	void beginGUI()
	{
		m_original_context = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGui::PushFont(m_font);
	}


	void endGUI()
	{
		ImGui::PopFont();
		ImGui::SetCurrentContext(m_original_context);
	}


	void onInputEvent(InputSystem::InputEvent& event)
	{
		beginGUI();
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		auto& io = ImGui::GetIO();
		io.MousePos = ImVec2(mouse_pos.x, mouse_pos.y);
		switch (event.type)
		{
			case InputSystem::InputEvent::POINTER_DOWN:
				io.MouseDown[0] = true;
				break;
			case InputSystem::InputEvent::POINTER_UP:
				io.MouseDown[0] = false;
				break;
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
