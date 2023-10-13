local rb_desc = createRenderbufferDesc { format = "rgba8", debug_name = "gui_editor" }

function main()
	local color_buffer = createRenderbuffer(rb_desc)
	setRenderTargets(color_buffer)
	clear(CLEAR_ALL, 0, 0, 0, 1, 0)
	render2D()
	setOutput(color_buffer)
end

