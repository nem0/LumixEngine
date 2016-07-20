#include "gui_system.h"
#include "engine/crc32.h"
#include "engine/delegate.h"
#include "engine/delegate_list.h"
#include "engine/engine.h"
#include "engine/iallocator.h"
#include "engine/input_system.h"
#include "engine/lua_wrapper.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/resource_manager.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/pipeline.h"
#include "renderer/shader.h"

#pragma warning(disable : 4267)
#include "tb_renderer_batcher.h"
#include <animation/tb_animation.h>
#include <bgfx/bgfx.h>
#include <tb_core.h>
#include <tb_font_renderer.h>
#include <tb_language.h>
#include <tb_msg.h>
#include <tb_node_tree.h>
#include <tb_renderer.h>
#include <tb_system.h>
#include <tb_widgets.h>
#include <tb_widgets_common.h>
#include <tb_widgets_reader.h>


using namespace tb;


namespace tb
{

void TBSystem::RescheduleTimer(double fire_time) {}

}


namespace Lumix
{


struct BGFXBitmap : public TBBitmap
{
	int Width() override { return w; }
	int Height() override { return h; }


	void SetData(uint32* data) override
	{
		pipeline->destroyTexture(tex);
		tex = pipeline->createTexture(w, h, data);
	}

	Pipeline* pipeline;
	bgfx::TextureHandle tex;
	int w, h;
};


struct GUIRenderer : public TBRendererBatcher
{
	GUIRenderer(Engine& engine)
		: m_pipeline(nullptr)
	{
		m_decl.begin()
			.add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
			.end();
		auto* material_manager = engine.getResourceManager().get(crc32("MATERIAL"));
		auto* resource = material_manager->load(Path("shaders/imgui.mat"));
		m_material = static_cast<Material*>(resource);
	}


	~GUIRenderer()
	{
		auto* material_manager = m_material->getResourceManager().get(crc32("MATERIAL"));
		material_manager->unload(*m_material);

		if (m_pipeline)
		{
			m_pipeline->destroyUniform(m_texture_uniform);
			auto* material_manager = m_material->getResourceManager().get(crc32("MATERIAL"));
			material_manager->unload(*m_material);
		}
	}


	TBBitmap* CreateBitmap(int width, int height, uint32* data) override
	{
		if (!m_pipeline) return nullptr;

		auto* bitmap = new BGFXBitmap();
		bitmap->tex = m_pipeline->createTexture(width, height, data);
		bitmap->w = width;
		bitmap->h = height;
		bitmap->pipeline = m_pipeline;
		return bitmap;
	}


	void RenderBatch(Batch* batch) override
	{
		if (!m_material->isReady()) return;
		if (!m_pipeline->checkAvailTransientBuffers(batch->vertex_count, m_decl, batch->vertex_count)) return;
		bgfx::TransientVertexBuffer vertex_buffer;
		bgfx::TransientIndexBuffer index_buffer;
		m_pipeline->allocTransientBuffers(
			&vertex_buffer, batch->vertex_count, m_decl, &index_buffer, batch->vertex_count);

		int16* idcs = (int16*)index_buffer.data;
		struct Vertex
		{
			float x, y;
			float u, v;
			uint32 color;
		};
		Vertex* vtcs = (Vertex*)vertex_buffer.data;
		for (int i = 0; i < batch->vertex_count; ++i)
		{
			idcs[i] = i;
			vtcs[i].x = batch->vertex[i].x;
			vtcs[i].y = batch->vertex[i].y;
			vtcs[i].u = batch->vertex[i].u;
			vtcs[i].v = batch->vertex[i].v;
			vtcs[i].color = batch->vertex[i].col;
		}

		if (batch->bitmap) m_pipeline->setTexture(0, ((BGFXBitmap*)(batch->bitmap))->tex, m_texture_uniform);

		m_pipeline->render(vertex_buffer,
			index_buffer,
			Matrix::IDENTITY,
			0,
			batch->vertex_count,
			BGFX_STATE_BLEND_ALPHA,
			m_material->getShaderInstance());
	}


	void SetClipRect(const TBRect& rect) override { m_pipeline->setScissor(rect.x, rect.y, rect.w, rect.h); }


	Material* m_material;
	bgfx::VertexDecl m_decl;
	Pipeline* m_pipeline;
	bgfx::UniformHandle m_texture_uniform;
};


struct GUISystemImpl : public GUISystem
{
	GUISystemImpl(Engine& engine)
		: m_engine(engine)
		, m_renderer(engine)
		, m_interface(nullptr)
	{
		tb_core_init(&m_renderer);

		engine.getInputSystem().eventListener().bind<GUISystemImpl, &GUISystemImpl::onInputEvent>(this);
		registerLuaAPI();
	}


	~GUISystemImpl()
	{
		m_engine.getInputSystem().eventListener().unbind<GUISystemImpl, &GUISystemImpl::onInputEvent>(this);
		tb_core_shutdown();
	}


	void showGUI(bool show)
	{
		m_root_widget.SetVisibility(show ? WIDGET_VISIBILITY_VISIBLE : WIDGET_VISIBILITY_GONE);
	}


	bool isGUIShown() const
	{
		return m_root_widget.GetVisibility() == WIDGET_VISIBILITY_VISIBLE;
	}


	void loadFile(const char* path)
	{
		m_root_widget.DeleteAllChildren();
		g_widgets_reader->LoadFile(&m_root_widget, path);
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

		REGISTER_FUNCTION(showGUI);
		REGISTER_FUNCTION(isGUIShown);
		REGISTER_FUNCTION(loadFile);

		LuaWrapper::createSystemVariable(L, "Gui", "instance", this);

		#undef REGISTER_FUNCTION
	}


	void setInterface(Interface* interface) override
	{
		// For safe typecasting
		if (m_renderer.m_pipeline)
		{
			m_renderer.m_pipeline->destroyUniform(m_renderer.m_texture_uniform);
		}

		m_renderer.m_pipeline = nullptr;

		m_interface = interface;
		
		if (!m_interface) return;
		
		auto* pipeline = m_interface->getPipeline();
		pipeline->addCustomCommandHandler("renderIngameGUI")
			.callback.bind<GUISystemImpl, &GUISystemImpl::pipelineCallback>(this);
		m_renderer.m_pipeline = m_interface->getPipeline();
		m_renderer.m_texture_uniform = m_renderer.m_pipeline->createTextureUniform("u_texture");

		void register_stb_font_renderer();
		register_stb_font_renderer();

		g_font_manager->AddFontInfo("gui/vera.ttf", "Vera");
		TBFontDescription fd;
		fd.SetID(TBIDC("Vera"));
		fd.SetSize(g_tb_skin->GetDimensionConverter()->DpToPx(14));
		g_font_manager->SetDefaultFontDescription(fd);

		TBFontFace* font = g_font_manager->CreateFontFace(g_font_manager->GetDefaultFontDescription());
		if (font)
			font->RenderGlyphs(
				" !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz");

		g_tb_lng->Load("gui/language/lng_en.tb.txt");
		g_tb_skin->Load("gui/skin/skin.tb.txt");
	}


	void pipelineCallback()
	{
		int w = (int)m_interface->getSize().x;
		int h = (int)m_interface->getSize().y;
		m_renderer.BeginPaint(w, h);

		Lumix::Matrix ortho;
		m_root_widget.SetSize(w, h);
		ortho.setOrtho(0.0f, (float)w, (float)h, 0.0f, -1.0f, 1.0f, false);
		m_interface->getPipeline()->setViewProjection(ortho, w, h);

		m_root_widget.InvokePaint(TBWidget::PaintProps());
		m_renderer.EndPaint();
	}


	void onInputEvent(InputSystem::InputEvent& event)
	{
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		switch (event.type)
		{
			case InputSystem::InputEvent::POINTER_DOWN:
				m_root_widget.InvokePointerDown((int)mouse_pos.x, (int)mouse_pos.y, 1, TB_MODIFIER_NONE, false);
				break;
			case InputSystem::InputEvent::POINTER_UP:
				m_root_widget.InvokePointerUp((int)mouse_pos.x, (int)mouse_pos.y, TB_MODIFIER_NONE, false);
				break;
			case InputSystem::InputEvent::POINTER_MOVE:
				m_root_widget.InvokePointerMove((int)mouse_pos.x, (int)mouse_pos.y, TB_MODIFIER_NONE, false);
				break;
			case InputSystem::InputEvent::KEY_DOWN:
				m_root_widget.InvokeKey(event.key.sym, TB_KEY_UNDEFINED, TB_MODIFIER_NONE, true);
				break;
			case InputSystem::InputEvent::KEY_UP:
				m_root_widget.InvokeKey(event.key.sym, TB_KEY_UNDEFINED, TB_MODIFIER_NONE, false);
				break;
		}
	}


	void update(float) override
	{
		float dx = m_engine.getInputSystem().getMouseXMove();
		float dy = m_engine.getInputSystem().getMouseYMove();
		Vec2 mouse_pos = m_engine.getInputSystem().getMousePos() - m_interface->getPos();
		if (dx != 0 || dy != 0)
		{
			m_root_widget.InvokePointerMove((int)mouse_pos.x, (int)mouse_pos.y, TB_MODIFIER_NONE, false);
		}

		TBAnimationManager::Update();
		m_root_widget.InvokeProcessStates();
		m_root_widget.InvokeProcess();
		TBMessageHandler::ProcessMessages();
	}


	const char* getName() const override { return "gui"; }


	Engine& m_engine;
	TBWidget m_root_widget;
	GUIRenderer m_renderer;
	Interface* m_interface;
};


LUMIX_PLUGIN_ENTRY(gui)
{
	return LUMIX_NEW(engine.getAllocator(), GUISystemImpl)(engine);
}


} // namespace Lumix
