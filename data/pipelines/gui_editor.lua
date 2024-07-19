local rb_desc = createRenderbufferDesc { format = "rgba8", debug_name = "gui_editor" }

function main()
	local color_buffer = createRenderbuffer(rb_desc)
	setRenderTargets(color_buffer)
	local clear_color = GUI_EDITOR_CLEAR_COLOR or {0, 0, 0}
	clear(CLEAR_ALL, clear_color[1], clear_color[2], clear_color[3], 1, 0)
	render2D()
	setOutput(color_buffer)
end

