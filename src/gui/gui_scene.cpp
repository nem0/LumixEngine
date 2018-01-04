#include "gui_scene.h"
#include "gui_system.h"
#include "engine/flag_set.h"
#include "engine/iallocator.h"
#include "engine/reflection.h"
#include "engine/universe/universe.h"
#include "renderer/draw2d.h"
#include "renderer/texture.h"


namespace Lumix
{


static const ComponentType GUI_RECT_TYPE = Reflection::getComponentType("gui_rect");
static const ComponentType GUI_IMAGE_TYPE = Reflection::getComponentType("gui_image");


struct GUISprite
{
	Texture* image = nullptr;
};


struct GUIImage
{
	GUISprite* sprite = nullptr;
	u32 color = 0xffffFFFF;
};


struct GUIRect
{
	enum Flags
	{
		IS_VALID = 1 << 0
	};

	struct Anchor
	{
		float points = 0;
		float relative = 0;
	};

	Entity entity;
	FlagSet<Flags, u8> flags;
	Anchor top;
	Anchor right;
	Anchor bottom;
	Anchor left;

	GUIImage* image = nullptr;
};


struct GUISceneImpl LUMIX_FINAL : public GUIScene
{
	GUISceneImpl(GUISystem& system, Universe& context, IAllocator& allocator)
		: m_allocator(allocator)
		, m_universe(context)
		, m_system(system)
		, m_sprites(allocator)
		, m_rects(allocator)
	{
		context.registerComponentType(GUI_RECT_TYPE, this, &GUISceneImpl::serializeRect, &GUISceneImpl::deserializeRect);
		context.registerComponentType(GUI_IMAGE_TYPE, this, &GUISceneImpl::serializeImage, &GUISceneImpl::deserializeImage);
	}


	struct Rect
	{
		float x, y, w, h;
	};


	void renderRect(GUIRect& rect, Draw2D& draw2d, const Rect& parent_rect)
	{
		if (!rect.flags.isSet(GUIRect::IS_VALID)) return;
		
		float l = parent_rect.x + rect.left.points + parent_rect.w * rect.left.relative;
		float r = parent_rect.x + rect.right.points + parent_rect.w * rect.right.relative;
		float t = parent_rect.y + rect.top.points + parent_rect.h * rect.top.relative;
		float b = parent_rect.y + rect.bottom.points + parent_rect.h * rect.bottom.relative;
			 
		if (rect.image) draw2d.AddRectFilled({l, t}, {r, b}, rect.image->color);

		Entity child = m_universe.getFirstChild(rect.entity);
		if (child.isValid())
		{
			int idx = m_rects.find(child);
			if (idx >= 0)
			{
				renderRect(*m_rects.at(idx), draw2d, { l, t, r - l, b - t });
			}
		}
		Entity sibling = m_universe.getNextSibling(rect.entity);
		if (sibling.isValid())
		{
			int idx = m_rects.find(sibling);
			if (idx >= 0)
			{
				renderRect(*m_rects.at(idx), draw2d, parent_rect);
			}
		}
	}


	void render(Draw2D& draw2d, const Vec2& canvas_size) override
	{
		if (!m_root) return;

		renderRect(*m_root, draw2d, {0, 0, canvas_size.x, canvas_size.y});
	}


	Vec4 getImageColorRGBA(ComponentHandle cmp) override
	{
		GUIImage* image = m_rects[{cmp.index}]->image;
		u32 color = image->color;
		float inv = 1 / 255.0f;
		return {
			((color >> 16) & 0xFF) * inv,
			((color >> 8) & 0xFF) * inv,
			((color >> 0) & 0xFF) * inv,
			((color >> 24) & 0xFF) * inv,
		};
	}


	void setImageColorRGBA(ComponentHandle cmp, const Vec4& color) override
	{
		GUIImage* image = m_rects[{cmp.index}]->image;
		u8 r = u8(color.x * 255 + 0.5f);
		u8 g = u8(color.y * 255 + 0.5f);
		u8 b = u8(color.z * 255 + 0.5f);
		u8 a = u8(color.w * 255 + 0.5f);
		image->color = (a << 24) + (r << 16) + (g << 8) + b;
	}


	float getRectLeftPoints(ComponentHandle cmp) override { return m_rects[{cmp.index}]->left.points; }
	void setRectLeftPoints(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->left.points = value; }
	float getRectLeftRelative(ComponentHandle cmp) override { return m_rects[{cmp.index}]->left.relative; }
	void setRectLeftRelative(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->left.relative = value; }

	float getRectRightPoints(ComponentHandle cmp) override { return m_rects[{cmp.index}]->right.points; }
	void setRectRightPoints(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->right.points = value; }
	float getRectRightRelative(ComponentHandle cmp) override { return m_rects[{cmp.index}]->right.relative; }
	void setRectRightRelative(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->right.relative = value; }

	float getRectTopPoints(ComponentHandle cmp) override { return m_rects[{cmp.index}]->top.points; }
	void setRectTopPoints(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->top.points = value; }
	float getRectTopRelative(ComponentHandle cmp) override { return m_rects[{cmp.index}]->top.relative; }
	void setRectTopRelative(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->top.relative = value; }

	float getRectBottomPoints(ComponentHandle cmp) override { return m_rects[{cmp.index}]->bottom.points; }
	void setRectBottomPoints(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->bottom.points = value; }
	float getRectBottomRelative(ComponentHandle cmp) override { return m_rects[{cmp.index}]->bottom.relative; }
	void setRectBottomRelative(ComponentHandle cmp, float value) override { m_rects[{cmp.index}]->bottom.relative = value; }


	void serialize(ISerializer& serializer) override
	{
	}


	void deserialize(IDeserializer& serializer) override
	{
	}


	void serializeRect(ISerializer&, ComponentHandle) {}


	void deserializeRect(IDeserializer&, Entity entity, int /*scene_version*/)
	{
	}


	void serializeImage(ISerializer&, ComponentHandle) {}


	void deserializeImage(IDeserializer&, Entity entity, int /*scene_version*/)
	{
	}


	void clear() override
	{
		 //TODO
	}


	void update(float time_delta, bool paused) override
	{
	}


	ComponentHandle createRect(Entity entity)
	{
		ComponentHandle cmp = { entity.index };
		
		int idx = m_rects.find(entity);
		GUIRect* rect;
		if (idx >= 0)
		{
			rect = m_rects.at(idx);
		}
		else
		{
			rect = LUMIX_NEW(m_allocator, GUIRect);
			m_rects.insert(entity, rect);
		}
		rect->entity = entity;
		rect->flags.set(GUIRect::IS_VALID);
		m_universe.addComponent(entity, GUI_RECT_TYPE, this, cmp);
		m_root = findRoot();

		return cmp;
	}


	ComponentHandle createImage(Entity entity)
	{
		int idx = m_rects.find(entity);
		if (idx < 0)
		{
			GUIRect* rect = LUMIX_NEW(m_allocator, GUIRect);
			rect->entity = entity;
			idx = m_rects.insert(entity, rect);
		}
		GUIRect& rect = *m_rects.at(idx);
		rect.image = LUMIX_NEW(m_allocator, GUIImage);
		ComponentHandle cmp = {entity.index};
		m_universe.addComponent(entity, GUI_IMAGE_TYPE, this, cmp);
		return cmp;
	}


	ComponentHandle createComponent(ComponentType type, Entity entity) override;


	GUIRect* findRoot()
	{
		if (m_rects.size() == 0) return nullptr;
		for (int i = 0, n = m_rects.size(); i < n; ++i)
		{
			GUIRect& rect = *m_rects.at(i);
			if (!rect.flags.isSet(GUIRect::IS_VALID)) continue;
			Entity e = m_rects.getKey(i);
			Entity parent = m_universe.getParent(e);
			if (parent == INVALID_ENTITY) return &rect;
			if (m_rects.find(parent) < 0) return &rect;
		}
		ASSERT(false);
		return nullptr;
	}


	void destroyRect(ComponentHandle component)
	{
		Entity entity = {component.index};
		GUIRect* rect = m_rects[entity];
		rect->flags.set(GUIRect::IS_VALID, false);
		if (rect->image == nullptr)
		{
			LUMIX_DELETE(m_allocator, rect);
			m_rects.erase(entity);
			
			if (rect == m_root)
			{
				m_root = findRoot();
			}
		}
		m_universe.destroyComponent(entity, GUI_RECT_TYPE, this, component);
	}


	void destroyImage(ComponentHandle component)
	{
		Entity entity = {component.index};
		GUIRect* rect = m_rects[entity];
		LUMIX_DELETE(m_allocator, rect->image);
		rect->image = nullptr;
		m_universe.destroyComponent(entity, GUI_IMAGE_TYPE, this, component);
	}


	void destroyComponent(ComponentHandle component, ComponentType type) override;


	void serialize(OutputBlob& serializer) override
	{

	}


	void deserialize(InputBlob& serializer) override
	{
	}


	ComponentHandle getComponent(Entity entity, ComponentType type) override
	{
		if (type == GUI_RECT_TYPE)
		{
			if (m_rects.find(entity) < 0) return INVALID_COMPONENT;
			return {entity.index};
		}
		if (type == GUI_IMAGE_TYPE)
		{
			int idx = m_rects.find(entity);
			if (idx < 0) return INVALID_COMPONENT;
			if (!m_rects.at(idx)->image) return INVALID_COMPONENT;
			return {entity.index};
		}
		return INVALID_COMPONENT;
	}

	Universe& getUniverse() override { return m_universe; }
	IPlugin& getPlugin() const override { return m_system; }

	IAllocator& m_allocator;
	Universe& m_universe;
	GUISystem& m_system;
	
	Array<GUISprite*> m_sprites;
	AssociativeArray<Entity, GUIRect*> m_rects;
	GUIRect* m_root = nullptr;
};


static struct
{
	ComponentType type;
	ComponentHandle(GUISceneImpl::*creator)(Entity);
	void (GUISceneImpl::*destroyer)(ComponentHandle);
} COMPONENT_INFOS[] = {
	{ GUI_RECT_TYPE, &GUISceneImpl::createRect, &GUISceneImpl::destroyRect },
	{ GUI_IMAGE_TYPE, &GUISceneImpl::createImage, &GUISceneImpl::destroyImage}
};


ComponentHandle GUISceneImpl::createComponent(ComponentType type, Entity entity)
{
	for(auto& i : COMPONENT_INFOS)
	{
		if(i.type == type)
		{
			return (this->*i.creator)(entity);
		}
	}
	return INVALID_COMPONENT;
}


void GUISceneImpl::destroyComponent(ComponentHandle component, ComponentType type)
{
	for(auto& i : COMPONENT_INFOS)
	{
		if(i.type == type)
		{
			(this->*i.destroyer)(component);
			return;
		}
	}
}


GUIScene* GUIScene::createInstance(GUISystem& system,
	Universe& universe,
	IAllocator& allocator)
{
	return LUMIX_NEW(allocator, GUISceneImpl)(system, universe, allocator);
}


void GUIScene::destroyInstance(GUIScene* scene)
{
	LUMIX_DELETE(static_cast<GUISceneImpl*>(scene)->m_allocator, scene);
}


} // namespace Lumix