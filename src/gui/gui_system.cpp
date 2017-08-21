#include "gui_system.h"
#include "draw_list.h"
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
		, m_2d_draw_list(engine.getAllocator())
		, m_2d_font_atlas(engine.getAllocator())
	{
		m_context = ImGui::CreateContext();
		m_original_context = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(m_context);
		ImGuiIO& io = ImGui::GetIO();
		
		m_font = io.Fonts->AddFontFromFileTTF("bin/VeraMono.ttf", 20);
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

		init2DDrawFont();

		io.DisplaySize.x = 640;
		io.DisplaySize.y = 480;
		ImGui::NewFrame();
		ImGui::SetCurrentContext(m_original_context);

		registerLuaAPI();
		m_2d_draw_list.Clear();
		m_2d_draw_list.PushClipRectFullScreen();
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


	void init2DDrawFont()
	{
		m_2d_font = m_2d_font_atlas.AddFontDefault();
		u8* pixels;
		int w, h;
		m_2d_font_atlas.GetTexDataAsRGBA32(&pixels, &w, &h);
		auto* material_manager = m_engine.getResourceManager().get(MATERIAL_TYPE);
		auto* resource = material_manager->load(Path("pipelines/imgui/2ddraw.mat"));
		m_2d_draw_material = static_cast<Material*>(resource);

		auto* old_texture = m_2d_draw_material->getTexture(0);
		Texture* texture = LUMIX_NEW(m_engine.getAllocator(), Texture)(
			Path("2d_draw_font"), *m_engine.getResourceManager().get(TEXTURE_TYPE), m_engine.getAllocator());

		texture->create(w, h, pixels);
		m_2d_draw_material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(m_engine.getAllocator(), old_texture);
		}
		
		m_2d_font_atlas.TexID = &texture->handle;
		m_2d_draw_list.FontTexUvWhitePixel = m_2d_font_atlas.TexUvWhitePixel;
	}


	float getTextWidth(const char* text)
	{
		return m_2d_font->CalcTextSizeA(13, FLT_MAX, 0, text).x;
	}


	void drawText(float font_size, float x, float y, u32 color, const char* text)
	{
		m_2d_draw_list.PushTextureID(m_2d_font->ContainerAtlas->TexID);
		m_2d_draw_list.AddText(m_2d_font, font_size, Vec2(x, y), color, text);
		m_2d_draw_list.PopTextureID();
	}


	void drawRect(float x0, float y0, float x1, float y1, u32 color)
	{
		m_2d_draw_list.AddRect(Vec2(x0, y0), Vec2(x1, y1), color);
	}


	void drawImage(int image_id, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1)
	{
		Texture* tex = (Texture*)m_engine.getLuaResource(image_id);
		float w = 1.0f / tex->width;
		float h = 1.0f / tex->height;
		m_2d_draw_list.AddImage(&tex->handle, Vec2(x0, y0), Vec2(x1, y1), Vec2(u0 * w, v0 * h), Vec2(u1 * w, v1 * h));
	}


	void drawRectFilled(float x0, float y0, float x1, float y1, u32 color)
	{
		m_2d_draw_list.AddRectFilled(Vec2(x0, y0), Vec2(x1, y1), color);
	}


	int getViewportWidth()
	{
		return m_interface->getPipeline()->getWidth();
	}


	int getViewportHeight()
	{
		return m_interface->getPipeline()->getHeight();
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

		REGISTER_FUNCTION(getViewportWidth);
		REGISTER_FUNCTION(getViewportHeight);
		REGISTER_FUNCTION(beginGUI);
		REGISTER_FUNCTION(endGUI);
		REGISTER_FUNCTION(enableCursor);
		REGISTER_FUNCTION(getMouseX);
		REGISTER_FUNCTION(getMouseY);
		REGISTER_FUNCTION(isMouseClicked);
		REGISTER_FUNCTION(getTextWidth);
		REGISTER_FUNCTION(drawText);
		REGISTER_FUNCTION(drawRect);
		REGISTER_FUNCTION(drawRectFilled);
		REGISTER_FUNCTION(drawImage);

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
		bool is_opengl = bgfx::getRendererType() == bgfx::RendererType::OpenGL ||
			bgfx::getRendererType() == bgfx::RendererType::OpenGLES;
		ortho.setOrtho(0.0f, size.x, size.y, 0.0f, -1.0f, 1.0f, is_opengl);
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


	void render2DDraw()
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		if (!renderer) return;

		Pipeline* pipeline = m_interface->getPipeline();
		int num_indices = m_2d_draw_list.IdxBuffer.size();
		int num_vertices = m_2d_draw_list.VtxBuffer.size();
		if (num_indices == 0) return;

		const bgfx::VertexDecl& decl = renderer->getBasic2DVertexDecl();
		bgfx::TransientVertexBuffer vertex_buffer;
		bgfx::TransientIndexBuffer index_buffer;
		if (bgfx::getAvailTransientIndexBuffer(num_indices) < (u32)num_indices) return;
		if (bgfx::getAvailTransientVertexBuffer(num_vertices, decl) < (u32)num_vertices) return;
		bgfx::allocTransientVertexBuffer(&vertex_buffer, num_vertices, decl);
		bgfx::allocTransientIndexBuffer(&index_buffer, num_indices);

		copyMemory(vertex_buffer.data, &m_2d_draw_list.VtxBuffer[0], num_vertices * decl.getStride());
		copyMemory(index_buffer.data, &m_2d_draw_list.IdxBuffer[0], num_indices * sizeof(u16));

		u32 elem_offset = 0;
		const DrawList::DrawCmd* pcmd_begin = m_2d_draw_list.CmdBuffer.begin();
		const DrawList::DrawCmd* pcmd_end = m_2d_draw_list.CmdBuffer.end();
		for (const DrawList::DrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
		{
			if (0 == pcmd->ElemCount) continue;

			pipeline->setScissor(u16(Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::maximum(pcmd->ClipRect.y, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.z, 65535.0f) - Math::maximum(pcmd->ClipRect.x, 0.0f)),
				u16(Math::minimum(pcmd->ClipRect.w, 65535.0f) - Math::maximum(pcmd->ClipRect.y, 0.0f)));

			const bgfx::TextureHandle& texture_id = 
				pcmd->TextureId ? *(bgfx::TextureHandle*)pcmd->TextureId : m_2d_draw_material->getTexture(0)->handle;
			auto texture_uniform = m_2d_draw_material->getShader()->m_texture_slots[0].uniform_handle;
			pipeline->setTexture(0, texture_id, texture_uniform);
			pipeline->render(vertex_buffer,
				index_buffer,
				Matrix::IDENTITY,
				elem_offset,
				pcmd->ElemCount,
				m_2d_draw_material->getRenderStates(),
				m_2d_draw_material->getShaderInstance());

			elem_offset += pcmd->ElemCount;
		}

		m_2d_draw_list.Clear();
		m_2d_draw_list.PushClipRectFullScreen();
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

		render2DDraw();

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
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		return mouse_pos.x;
	}


	bool isMouseClicked(int button)
	{
		return ImGui::IsMouseClicked(button);
	}


	float getMouseY() const
	{
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		return mouse_pos.y;
	}


	void update(float time_delta) override
	{
		beginGUI();
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		auto& io = ImGui::GetIO();
		io.MousePos = ImVec2(mouse_pos.x, mouse_pos.y);
		io.MouseDown[0] = m_engine.getInputSystem().isMouseDown(InputSystem::LEFT);
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
	Material* m_2d_draw_material;
	DrawList m_2d_draw_list;
	FontAtlas m_2d_font_atlas;
	Font* m_2d_font;
};


LUMIX_PLUGIN_ENTRY(gui)
{
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
