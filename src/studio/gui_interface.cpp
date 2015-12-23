#include "gui_interface.h"
#include "core/iallocator.h"


struct GUIInterfaceImpl : public GUIInterface
{
	GUIInterfaceImpl(Lumix::IAllocator& allocator)
		: m_allocator(allocator)
	{
	}


	~GUIInterfaceImpl() { ImGui::Shutdown(); }


	ImGuiIO& getIO() override { return ImGui::GetIO(); }
	ImGuiStyle& getStyle() override { return ImGui::GetStyle(); }
	bool beginMainMenuBar() override { return ImGui::BeginMainMenuBar(); }
	void endMainMenuBar() override { ImGui::EndMainMenuBar(); }
	bool checkbox(const char* label, bool* v) override { return ImGui::Checkbox(label, v); }

	bool collapsingHeader(const char* label,
		const char* str_id = NULL,
		bool display_frame = true,
		bool default_open = false) override
	{
		return ImGui::CollapsingHeader(label, str_id, display_frame, default_open);
	}


	bool sliderFloat(const char* label,
		float* v,
		float v_min,
		float v_max,
		const char* display_format = "%.3f",
		float power = 1.0f) override
	{
		return ImGui::SliderFloat(label, v, v_min, v_max, display_format, power);
	}


	void nextColumn() override
	{
		ImGui::NextColumn();
	}


	void columns(int count = 1, const char* id = NULL, bool border = true) override
	{
		ImGui::Columns(count, id, border);
	}


	bool dragFloat(const char* label,
		float* v,
		float v_speed = 1.0f,
		float v_min = 0.0f,
		float v_max = 0.0f,
		const char* display_format = "%.3f",
		float power = 1.0f) override
	{
		return ImGui::DragFloat(label, v, v_speed, v_min, v_max, display_format, power);
	}


	bool inputText(const char* label,
		char* buf,
		size_t buf_size,
		ImGuiInputTextFlags flags = 0,
		ImGuiTextEditCallback callback = NULL,
		void* user_data = NULL) override
	{
		return ImGui::InputText(label, buf, buf_size, flags, callback, user_data);
	}


	bool beginMenu(const char* label, bool enabled) override
	{
		return ImGui::BeginMenu(label, enabled);
	}


	void endMenu() override { ImGui::EndMenu(); }


	bool beginChild(const char* str_id,
		const ImVec2& size = ImVec2(0, 0),
		bool border = false,
		ImGuiWindowFlags extra_flags = 0) override
	{
		return ImGui::BeginChild(str_id, size, border, extra_flags);
	}


	bool beginChild(ImGuiID id,
		const ImVec2& size = ImVec2(0, 0),
		bool border = false,
		ImGuiWindowFlags extra_flags = 0) override
	{
		return ImGui::BeginChild(id, size, border, extra_flags);
	}


	void endChild() override { ImGui::EndChild(); }


	void sameLine(float local_pos_x = 0.0f, float spacing_w = -1.0f) override
	{
		ImGui::SameLine(local_pos_x, spacing_w);
	}


	void indent() override { ImGui::Indent(); }
	void unindent() override { ImGui::Unindent(); }


	bool menuItem(const char* label,
		const char* shortcut = NULL,
		bool selected = false,
		bool enabled = true) override
	{
		return ImGui::MenuItem(label, shortcut, selected, enabled);
	}


	bool menuItem(const char* label,
		const char* shortcut,
		bool* p_selected,
		bool enabled = true) override
	{
		return ImGui::MenuItem(label, shortcut, p_selected, enabled);
	}


	void end() override { ImGui::End(); }
	bool begin(const char* name, bool* p_opened = NULL, ImGuiWindowFlags flags = 0) override
	{
		return ImGui::Begin(name, p_opened, flags);
	}


	bool begin(const char* name,
		bool* p_opened,
		const ImVec2& size_on_first_use,
		float bg_alpha = -1.0f,
		ImGuiWindowFlags flags = 0)
	{
		return ImGui::Begin(name, p_opened, size_on_first_use, bg_alpha, flags);
	}


	void text(const char* text, ...) override
	{
		va_list args;
		va_start(args, text);
		ImGui::TextV(text, args);
		va_end(args);
	}
	void separator() override { ImGui::Separator(); }
	void bulletText(const char* text) override { ImGui::BulletText(text); }

	bool button(const char* label, const ImVec2 size = ImVec2(0, 0)) override
	{
		return ImGui::Button(label, size);
	}


	Lumix::IAllocator& m_allocator;
};


GUIInterface* GUIInterface::create(Lumix::IAllocator& allocator)
{
	return LUMIX_NEW(allocator, GUIInterfaceImpl)(allocator);
}


void GUIInterface::destroy(GUIInterface& instance)
{
	auto& impl = static_cast<GUIInterfaceImpl&>(instance);
	LUMIX_DELETE(impl.m_allocator, &impl);
}
