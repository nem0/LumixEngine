
local debugger_started = false
local debug_stream = nil
local DEBUG_HEADER_MAGIC = "LumixLuaDebuggerHeader";
local callstack = {}
local locals = {}

function closeDebugConnection(is_error)
	LumixAPI.networkClose(debug_stream)
	debug_stream = nil
	if is_error then 
		LumixAPI.logError("Lua debugger - connection error") 
	end
end

-- disabled by default, since networkConnect's timeout is annoying
if false then
	_G.LumixDebugCallback = function()
		debug_stream = LumixAPI.networkConnect("127.0.0.1", 56789)
		if debug_stream then 
			local header = LumixAPI.networkRead(debug_stream, 22) -- 22 = LumixLuaDebuggerHeader
			if header ~= DEBUG_HEADER_MAGIC then
				LumixAPI.logError("Lua debug client - invalid header" .. tostring(header))
				closeDebugConnection()
				return
			end
			LumixAPI.networkWrite(debug_stream, DEBUG_HEADER_MAGIC, 22)
			while debug_stream do
				local msg_type = recv()
				if msg_type == "end" then
					closeDebugConnection()
					return
				end
				if msg_type == "execute" then
					local cmd = recv()
					local func, err = load(cmd)
					if func then
						local res = func()
						send(res)
					else
						LumixAPI.logError("Lua debugger client - execute failed: " .. err)
						closeDebugConnection(true)
					end
				else 
					LumixAPI.logError("Lua debug client: Unknown type " .. tostring(msg_type))
					closeDebugConnection(true)
				end
			end
		end
	end

	function send(msg)
		if msg == nil then msg = "" end
		if not debug_stream then return end
		local msg_len = string.len(msg)
		local packed_len = LumixAPI.packU32(msg_len)
		local res = LumixAPI.networkWrite(debug_stream, packed_len, string.len(packed_len))
		res = res and LumixAPI.networkWrite(debug_stream, msg, msg_len)
		if not res then closeDebugConnection(true) end
	end

	function recv()
		if not debug_stream then return end
		local packed_len = LumixAPI.networkRead(debug_stream, 4)
		if packed_len == nil then 
			closeDebugConnection(true)
			return
		end
		local len = LumixAPI.unpackU32(packed_len)

		local msg = ""
		if len == 0 then 
			msg = "" 
			return msg
		end

		msg = LumixAPI.networkRead(debug_stream, len)
		if msg == nil then 
			closeDebugConnection(true)
		end
		return msg
	end

	function gatherLocals(level)
		locals = {}
		local i = 0
		while debug_stream do
			i = i + 1
			local l = {}
			LumixAPI.logInfo("locals " .. tostring(i) .. " " .. tostring(level))
			send("execute")
			send("local name, val = debug.getlocal(" ..  tostring(level + 3) .. ", " .. tostring(i) .. "); if not name then return \"\" end; return tostring(name)");
			l.name = recv()
			LumixAPI.logInfo("wtf")
			if l.name == "" then return end
			LumixAPI.logInfo("wtf2" .. l.name)
			send("execute")
			send("local name, val = debug.getlocal(" .. tostring(level + 3) ..  ", " .. tostring(i) .. "); return tostring(val)");
			LumixAPI.logInfo("wtf3")
			
			l.value = recv()

			table.insert(locals, l)
		end
	end

	function gatherCallstack()
		local level = 4
		callstack = {}
		while debug_stream do
			local line = {}

			send("execute");
			send("local info = debug.getinfo(" .. level .. ") if info then return info.source else return \"\" end")
			line.src = recv()
			if line.src == "" then return end
			send("execute");
			send("return debug.getinfo(" .. level .. ").name")
			line.name = recv()
			send("execute");
			send("return debug.getinfo(" .. level .. ").currentline")
			line.line = recv()

			level = level + 1

			table.insert(callstack, line)
		end
	end

	function startDebugger()
		debug_stream = LumixAPI.networkListen("127.0.0.1", 56789)
		if debug_stream then
			LumixAPI.networkWrite(debug_stream, DEBUG_HEADER_MAGIC, 22)
			local header = LumixAPI.networkRead(debug_stream, 22)
			if header ~= DEBUG_HEADER_MAGIC then
				LumixAPI.logError("Lua debug server - invalid header")
				closeDebugConnection()
				return
			end
			gatherCallstack()
			debugger_started = true
		end
	end

	function debuggerUI()
		if ImGui.Begin("Lua debugger") then
			if ImGui.Button("Disconnect") then
				send("end")
				LumixAPI.networkClose(debug_stream)
				debug_stream = nil
				debugger_started = false
			end
			for k, v in ipairs(callstack) do
				if ImGui.Selectable(tostring(v.src) .. ": " .. tostring(v.name) .. ": " .. tostring(v.line)) then
					gatherLocals(k)
				end
			end

			for k, v in ipairs(locals) do
				ImGui.Text(v.name .. " = " .. v.value)
			end
		end
		ImGui.End()
	end

	return coroutine.create(function() 
		while true do
			if debugger_started then 
				debuggerUI() 
			else
				ImGui.Begin("Lua console##lua_console")
				if ImGui.Button("Start debugger") then startDebugger() end
				ImGui.SameLine()
				ImGui.End()
			end
			coroutine.yield()
		end
	end)
end

