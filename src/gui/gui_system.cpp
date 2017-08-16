#include "gui_system.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
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
#include "html_document_container.h"


namespace Lumix
{


struct GUISystemImpl;



struct GUISystemImpl LUMIX_FINAL : public GUISystem
{
	GUISystemImpl(Engine& engine)
		: m_engine(engine)
		, m_interface(nullptr)
		, m_html_container(engine)
	{
		registerLuaAPI();
		m_engine.getInputSystem().addAction(crc32("ui_mouse_down"), InputSystem::InputType::PRESSED, 1, -1);
	}


	bool loadFile(const char* path, Array<u8>* out_data)
	{
		ASSERT(out_data);

		FS::OsFile file;
		if (!file.open(path, FS::Mode::OPEN_AND_READ, m_engine.getAllocator())) return false;

		out_data->resize((int)file.size());
		bool success = file.read(&(*out_data)[0], out_data->size());
		file.close();
		
		return success;
	}


	bool init()
	{
		Array<u8> css(m_engine.getLIFOAllocator());
		if (!loadFile("master.css", &css)) return false;
		
		m_html_context.load_master_stylesheet((litehtml::tchar_t*)&css[0]);

		ResourceManagerBase* material_manager = m_engine.getResourceManager().get(MATERIAL_TYPE);
		Resource* resource = material_manager->load(Path("pipelines/gui/gui.mat"));
		m_material = (Material*)resource;

		return true;
	}


	std::shared_ptr<litehtml::element> findChild(std::shared_ptr<litehtml::element> parent, const char* tag)
	{
		int count = (int)parent->get_children_count();
		for (int i = 0; i < count; ++i)
		{
			std::shared_ptr<litehtml::element> child = parent->get_child(i);
			if (equalStrings(child->get_tagName(), tag)) return child;
		}
		return nullptr;
	}


	bool openDocument(const char* path)
	{
		Array<u8> page(m_engine.getLIFOAllocator());

		if (!loadFile(path, &page)) return false;
		
		m_document = litehtml::document::createFromUTF8((const char*)&page[0], &m_html_container, &m_html_context);
		if (!m_document) return false;
		
		m_document->render(1024);

		auto head = findChild(m_document->root(), "head");
		if (head)
		{
			auto script_el = findChild(head, "script");

			if (script_el)
			{
				auto script = std::static_pointer_cast<litehtml::el_script>(script_el);
				lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment);
				const auto& code = script->get_scriptSource();
				bool errors = luaL_loadbuffer(m_state,
					code.c_str(),
					code.length(),
					"gui script") != LUA_OK;

				if (errors)
				{
					g_log_error.log("Lua Script") << "gui script: " << lua_tostring(m_state, -1);
					lua_pop(m_state, 1);
					return false;
				}

				lua_pushvalue(m_state, -2);
				lua_setupvalue(m_state, -2, 1); // function's environment

				errors = errors || lua_pcall(m_state, 0, 0, 0) != LUA_OK;
				if (errors)
				{
					g_log_error.log("Lua Script") << "gui script: " << lua_tostring(m_state, -1);
					lua_pop(m_state, 1);
				}
				lua_pop(m_state, 1);
			}
		}
		return true;
	}


	~GUISystemImpl()
	{
		luaL_unref(m_state, LUA_REGISTRYINDEX, m_thread_ref);
		luaL_unref(m_state, LUA_REGISTRYINDEX, m_environment);

		Texture* texture = m_material->getTexture(0);
		if (texture)
		{
			m_material->setTexture(0, nullptr);
			texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), texture);
		}

		m_material->getResourceManager().unload(*m_material);
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

		REGISTER_FUNCTION(init);
		REGISTER_FUNCTION(openDocument);
		REGISTER_FUNCTION(enableCursor);

		LuaWrapper::createSystemVariable(L, "Gui", "instance", this);

		#undef REGISTER_FUNCTION

		m_state = lua_newthread(L);
		lua_pushvalue(L, -1);
		m_thread_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_pop(L, 1);
		lua_newtable(m_state);
		// reference environment
		lua_pushvalue(m_state, -1);
		m_environment = luaL_ref(m_state, LUA_REGISTRYINDEX);

		// environment's metatable & __index
		lua_pushvalue(m_state, -1);
		lua_setmetatable(m_state, -2);
		lua_pushglobaltable(m_state);
		lua_setfield(m_state, -2, "__index");
	}


	bool call(const char* function)
	{
		if (!m_state) return false;

		bool is_env_valid = lua_rawgeti(m_state, LUA_REGISTRYINDEX, m_environment) == LUA_TTABLE;
		ASSERT(is_env_valid);
		if (lua_getfield(m_state, -1, function) != LUA_TFUNCTION)
		{
			lua_pop(m_state, 2);
			return false;
		}

		if (lua_pcall(m_state, 0, 0, 0) != LUA_OK)
		{
			g_log_warning.log("GUI") << lua_tostring(m_state, -1);
			lua_pop(m_state, 1);
		}
		lua_pop(m_state, 1);
		return true;
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
		bool is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
			bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
		ortho.setOrtho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f, is_opengl);
		pipeline->setViewProjection(ortho, (int)size.x, (int)size.y);
	}


	void renderUI()
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		if (!renderer) return;

		Pipeline* pipeline = m_interface->getPipeline();
		int num_indices = m_html_container.m_draw_list.IdxBuffer.size();
		int num_vertices = m_html_container.m_draw_list.VtxBuffer.size();
		if (num_indices == 0) return;

		const bgfx::VertexDecl& decl = renderer->getBasic2DVertexDecl();
		bgfx::TransientVertexBuffer vertex_buffer;
		bgfx::TransientIndexBuffer index_buffer;
		if (bgfx::getAvailTransientIndexBuffer(num_indices) < (u32)num_indices) return;
		if (bgfx::getAvailTransientVertexBuffer(num_vertices, decl) < (u32)num_vertices) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &m_html_container.m_draw_list.VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &m_html_container.m_draw_list.IdxBuffer[0], num_indices * sizeof(u16));

		u32 elem_offset = 0;
		const DrawList::DrawCmd* pcmd_begin = m_html_container.m_draw_list.CmdBuffer.begin();
		const DrawList::DrawCmd* pcmd_end = m_html_container.m_draw_list.CmdBuffer.end();
		for (const DrawList::DrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if (0 == pcmd->ElemCount) continue;

			pipeline->setScissor(u16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

			const bgfx::TextureHandle& texture_id =
				pcmd->TextureId ? *(bgfx::TextureHandle*)pcmd->TextureId : m_material->getTexture(0)->handle;
			auto texture_uniform = m_material->getShader()->m_texture_slots[0].uniform_handle;
			pipeline->setTexture(0, texture_id, texture_uniform);
			pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				m_material->getRenderStates(),
				m_material->getShaderInstance());

			elem_offset += pcmd->ElemCount;
		}
	}


	void pipelineCallback()
	{
		if (!m_interface) return;
		Pipeline* pipeline = m_interface->getPipeline();
		if (!pipeline->isReady()) return;

		if (m_document)
		{
			m_html_container.m_pos.x = 0;
			m_html_container.m_pos.y = 0;
			m_html_container.m_draw_list.Clear();
			m_html_container.m_draw_list.PushClipRectFullScreen();
			litehtml::position clip(0, 0, pipeline->getWidth(), pipeline->getHeight());
			m_document->draw((litehtml::uint_ptr)this, 0, 0, &clip);
		}

		setGUIProjection();

		renderUI();
	}


	void update(float time_delta) override 
	{
		if (!m_document) return;

		InputSystem& input = m_engine.getInputSystem();
		static const u32 UI_MOUSE_DOWN_HASH = crc32("ui_mouse_down");
		Vec2 mpos = input.getMousePos();
		mpos = mpos - m_interface->getPos();
		std::vector<litehtml::position> boxes;
		bool is_down = input.getActionValue(UI_MOUSE_DOWN_HASH) > 0;
		if (is_down && !m_was_lbutton_down)
		{
			std::shared_ptr<litehtml::element> el = m_document->root()->get_element_by_point((int)mpos.x, (int)mpos.y, (int)mpos.x, (int)mpos.y);
			if (el)
			{
				const char* attr = el->get_attr("onclick");
				if (attr)
				{
					call(attr);
				}
			}
			
			m_was_lbutton_down = true;
			m_document->on_lbutton_down((int)mpos.x, (int)mpos.y, (int)mpos.x, (int)mpos.y, boxes);
		}
		else if (m_was_lbutton_down && !is_down)
		{
			m_was_lbutton_down = false;
			m_document->on_lbutton_up((int)mpos.x, (int)mpos.y, (int)mpos.x, (int)mpos.y, boxes);
		}
		m_document->on_mouse_over((int)mpos.x, (int)mpos.y, (int)mpos.x, (int)mpos.y, boxes);
	}


	void stopGame() override {}


	const char* getName() const override { return "gui"; }


	Engine& m_engine;
	Interface* m_interface;
	Material* m_material;
	lua_State* m_state;
	int m_thread_ref;
	int m_environment;

	bool m_was_lbutton_down = false;
	litehtml::context m_html_context;
	HTMLDocumentContainer m_html_container;
	std::shared_ptr<litehtml::document> m_document;
};


LUMIX_PLUGIN_ENTRY(gui)
{
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
