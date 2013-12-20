#pragma once


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
			typedef void (*EventCallback)(Block& block);
			typedef void (*KeyEventCallback)(int key, Block& block);

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
			void destroy();
			void setArea(float rel_left, float left, float rel_top, float top, float rel_right, float right, float rel_bottom, float bottom);
			float getGlobalTop() const { return m_global_area.top; }
			float getGlobalRight() const { return m_global_area.right; }
			float getGlobalBottom() const { return m_global_area.bottom; }
			float getGlobalLeft() const { return m_global_area.left; }
			Area& getGlobalArea() { return m_global_area; }
			Area& getClickArea() { return m_click_area; }
			void setText(const char* text) { m_text = text; }
			const string& getText() const { return m_text; }
			bool isShown() const { return m_is_shown; }
			void hide() { m_is_shown = false; }
			void show() { m_is_shown = true; }
			void setParent(Block* block);
			Block* getParent() const { return m_parent; }
			int getChildCount() const { return m_children.size(); }
			Block* getChild(int index) { return m_children.size() > index ? m_children[index] : NULL; }
			Gui* getGui() const { return m_gui; }
			void render(IRenderer& renderer);
			void layout();
			float getWidth() const { return m_local_area.right - m_local_area.left; }
			float getHeight() const { return m_local_area.bottom - m_local_area.top; }
			void* getTag() const { return m_tag; }
			void setTag(void* tag) { m_tag = tag; }
			bool click(int x, int y);
			void registerEventHandler(const char* type, const char* callback);
			void setZIndex(int z_index);
			float getZ() const { return m_z; }
			void setFocusProcessing() { m_is_focus_processing = true; }
			void emitEvent(const char* type);
			void setIsClickable(bool clickable) { m_is_mouse_clickable = clickable; }

			void serialize(ISerializer& serializer);
			void deserialize(ISerializer& serializer);

		private:
			struct EventHandler
			{
				EventHandler() {}
				uint32_t type;
				union 
				{
					EventCallback callback;
					KeyEventCallback key_callback;
				};
			};

		private:
			Block(Gui& gui, Block* parent, const char* decorator_name);
			~Block();

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
			Area m_click_area;
			string m_text;
			bool m_is_shown;
			bool m_is_dirty_layout;
			bool m_is_floating;
			bool m_fit_content;
			bool m_is_focus_processing;
			bool m_is_mouse_clickable;
			Gui* m_gui;
			void* m_tag;
			float m_z;
	};


} // ~namespace UI
} // ~namespace Lux