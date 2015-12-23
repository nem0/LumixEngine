#pragma once


#include "ocornut-imgui/imgui.h"


namespace Lumix
{
class IAllocator;
}


class GUIInterface
{
public:
	virtual ImGuiIO& getIO() = 0;
	virtual ImGuiStyle& getStyle() = 0;

	virtual bool dragFloat(const char* label,
		float* v,
		float v_speed = 1.0f,
		float v_min = 0.0f,
		float v_max = 0.0f,
		const char* display_format = "%.3f",
		float power = 1.0f) = 0;

	virtual bool sliderFloat(const char* label,
		float* v,
		float v_min,
		float v_max,
		const char* display_format = "%.3f",
		float power = 1.0f) = 0;

	virtual void columns(int count = 1, const char* id = NULL, bool border = true) = 0;
	virtual void nextColumn() = 0;

	virtual bool collapsingHeader(const char* label,
		const char* str_id = NULL,
		bool display_frame = true,
		bool default_open = false) = 0;

	virtual bool button(const char* label, const ImVec2 size = ImVec2(0, 0)) = 0;
	virtual void text(const char* text, ...) = 0;
	virtual void bulletText(const char* text) = 0;
	virtual void separator() = 0;
	virtual bool begin(const char* name, bool* p_opened = NULL, ImGuiWindowFlags flags = 0) = 0;
	virtual bool begin(const char* name,
		bool* p_opened,
		const ImVec2& size_on_first_use,
		float bg_alpha = -1.0f,
		ImGuiWindowFlags flags = 0) = 0;
	virtual void end() = 0;
	virtual bool menuItem(const char* label,
		const char* shortcut,
		bool* p_selected,
		bool enabled = true) = 0;
	virtual bool menuItem(const char* label,
		const char* shortcut = NULL,
		bool selected = false,
		bool enabled = true) = 0;
	virtual bool beginMainMenuBar() = 0;
	virtual void endMainMenuBar() = 0;
	virtual bool beginMenu(const char* label, bool enabled = true) = 0;
	virtual void endMenu() = 0;

	virtual bool checkbox(const char* label, bool* v) = 0;

	virtual bool inputText(const char* label,
		char* buf,
		size_t buf_size,
		ImGuiInputTextFlags flags = 0,
		ImGuiTextEditCallback callback = NULL,
		void* user_data = NULL) = 0;

	virtual bool beginChild(const char* str_id,
		const ImVec2& size = ImVec2(0, 0),
		bool border = false,
		ImGuiWindowFlags extra_flags = 0) = 0;
	virtual bool beginChild(ImGuiID id,
		const ImVec2& size = ImVec2(0, 0),
		bool border = false,
		ImGuiWindowFlags extra_flags = 0) = 0;
	virtual void endChild() = 0;
	virtual void indent() = 0;
	virtual void unindent() = 0;
	virtual void sameLine(float local_pos_x = 0.0f, float spacing_w = -1.0f) = 0;

	static GUIInterface* create(Lumix::IAllocator& allocator);
	static void destroy(GUIInterface& instance);
};