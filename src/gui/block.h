#pragma once


#include "core/delegate.h"
#include "core/lux.h"
#include "core/vector.h"
#include "core/string.h"


namespace Lux
{

class ISerializer;
	
namespace UI
{

	class Gui;
	class DecoratorBase;
	class IRenderer;


	class LUX_GUI_API Block
	{
		friend class Gui;
		public:
			typedef Delegate<void (Block&, void*)> EventCallback;

			struct Area	
			{
				void merge(const Area& area);
				
				float left;
				float right;
				float top;
				float bottom;
				float rel_left;
				float rel_right;
				float rel_top;
				float rel_bottom;
			};

		public:
			Block(Gui& gui, Block* parent, const char* decorator_name);
			virtual ~Block();
			virtual uint32_t getType() const;
			virtual void layout();
			void destroy();
			void setArea(float rel_left, float left, float rel_top, float top, float rel_right, float right, float rel_bottom, float bottom);
			void setArea(const Area& area);
			float getGlobalTop() const { return m_global_area.top; }
			float getGlobalRight() const { return m_global_area.right; }
			float getGlobalBottom() const { return m_global_area.bottom; }
			float getGlobalLeft() const { return m_global_area.left; }
			Area& getGlobalArea() { return m_global_area; }
			float getGlobalWidth() const { return m_global_area.right - m_global_area.left; }
			float getGlobalHeight() const { return m_global_area.bottom - m_global_area.top; }
			Area& getContentArea() { return m_content_area; }
			Area& getLocalArea() { return m_local_area; }
			void setBlockText(const char* text) { m_text = text; }
			const string& getBlockText() const { return m_text; }
			bool isShown() const { return m_is_shown; }
			bool isClipping() const { return m_is_clipping; }
			void setIsClipping(bool is_clipping) { m_is_clipping = is_clipping; }
			void hide() { m_is_shown = false; }
			void show() { m_is_shown = true; }
			void setParent(Block* block);
			Block* getParent() const { return m_parent; }
			int getChildCount() const { return m_children.size(); }
			Block* getChild(int index) const { return m_children.size() > index ? m_children[index] : NULL; }
			Gui* getGui() const { return m_gui; }
			void render(IRenderer& renderer);
			float getWidth() const { return m_local_area.right - m_local_area.left; }
			float getHeight() const { return m_local_area.bottom - m_local_area.top; }
			void* getTag() const { return m_tag; }
			void setTag(void* tag) { m_tag = tag; }
			bool click(int x, int y);
			bool mouseDown(int x, int y);
			Block::EventCallback& getCallback(const char* type);
			void registerEventHandler(const char* type, const char* callback);
			void setZIndex(int z_index);
			float getZ() const { return m_z; }
			void setFocusProcessing() { m_is_focus_processing = true; }
			void emitEvent(const char* type);
			void setIsClickable(bool clickable) { m_is_mouse_clickable = clickable; }

			virtual void serialize(ISerializer& serializer);
			virtual void deserialize(ISerializer& serializer);
			void serializeWOChild(ISerializer& serializer);
			void deserializeWOChild(ISerializer& serializer);

		private:
			struct EventHandler
			{
				EventHandler() {}
				uint32_t type;
				EventCallback callback;
			};

		private:
			void addChild(Block& block);
			void removeChild(Block& block);
			EventHandler* getEventHandler(uint32_t type);
			void blur();
			void focus();

		private:
			DecoratorBase* m_decorator;
			Block* m_parent;
			vector<Block*> m_children; 
			vector<EventHandler> m_event_handlers; 
			Area m_local_area;
			Area m_global_area;
			Area m_content_area;
			string m_text;
			bool m_is_shown;
			bool m_is_dirty_layout;
			bool m_is_floating;
			bool m_fit_content;
			bool m_is_focus_processing;
			bool m_is_mouse_clickable;
			bool m_is_clipping;
			Gui* m_gui;
			void* m_tag;
			float m_z;
	};


} // ~namespace UI
} // ~namespace Lux