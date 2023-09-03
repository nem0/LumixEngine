
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


--[[
	struct NetworkDebugHeader {
		static constexpr u32 MAGIC = 'LDMG';
		u32 magic = MAGIC;
		u32 version = 0;
		u8 is_debugger = 1;
	};

	enum class NetworkDebugType : u8 {
		END,
		EXECUTE,
		ERROR
	};

	static void debugCallback(lua_State* L, void* user_data) {
		ConsolePlugin* that = (ConsolePlugin*)user_data;
		os::initNetwork();	
		os::NetworkStream* stream = os::connect("127.0.0.1", 56789, that->app.getAllocator());
		if (!stream) return;

		NetworkDebugHeader header;
		header.is_debugger = false;
		NetworkDebugHeader in_header;

		#define READ(V) if (!os::read(*stream, &(V), sizeof(V))) return fail()
		#define WRITE(V) if (!os::write(*stream, &(V), sizeof(V))) return fail()
		#define WRITE_CONST(V) { auto v = V; if (!os::write(*stream, &(v), sizeof(v))) return fail(); }

		auto fail = [&]() -> void {
			logError("Lua debugger network fail");
			os::close(*stream);
		};

		if (!os::write(*stream, &header, sizeof(header))) return fail();
		READ(in_header);
		if (in_header.magic != NetworkDebugHeader::MAGIC) return fail();
		if (in_header.version > 0) return fail();
		if (!in_header.is_debugger) return fail();

		OutputMemoryStream blob(that->app.getAllocator());
		for (;;) {
			NetworkDebugType type;
			READ(type);
			switch (type) {
				default:
				case NetworkDebugType::END:
				case NetworkDebugType::ERROR:
					os::close(*stream);
					return;
				case NetworkDebugType::EXECUTE: {
					u32 size;
					READ(size);
					blob.resize(size);
					if (!os::read(*stream, blob.getMutableData(), size)) return fail();
					StringView sv;
					sv.begin = (const char*)blob.data();
					sv.end = sv.begin + blob.size();
					
					if (LuaWrapper::execute(L, sv, "debugger", 1)) {
						WRITE_CONST(NetworkDebugType::EXECUTE);
						auto tt = lua_type(L, -1);
						const char* str = lua_tostring(L, -1);
						u32 len = str ? stringLength(str) : 0;
						WRITE_CONST(len);
						if (str && !os::write(*stream, str, len)) return fail();
						lua_pop(L, 1);
					}
					else {
						WRITE_CONST(NetworkDebugType::ERROR);
					}
					break;
				}
			}
		}
		
		#undef READ
		#undef WRITE
	}


	struct Debugger {
		struct CallstackItem {
			CallstackItem(IAllocator& allocator) : source(allocator), name(allocator) {}
			String source;
			String name;
			u32 current_line = 0xffFFffFF;
		};
		
		struct Local {
			Local(IAllocator& allocator) : name(allocator), value(allocator) {}
			String name;
			String value;
		};

		Debugger(IAllocator& allocator)
			: m_allocator(allocator)
			, m_callstack(allocator)
			, m_locals(allocator)
			, m_blob(allocator)
			, m_eval_result(allocator)
		{}

		bool writeString(os::NetworkStream& stream, const char* v) {
			u32 size = stringLength(v);
			if (!os::write(stream, &size, sizeof(size))) return false;
			return os::write(stream, v, size);
		}

		void run() {
			os::initNetwork();
			m_stream = os::listen("127.0.0.1", 56789, m_allocator);
			if (!m_stream) return;

			NetworkDebugHeader in_header;
			NetworkDebugHeader header;
			header.is_debugger = true;
		
			#define READ(V) if (!os::read(*m_stream, &(V), sizeof(V))) goto fail
			#define WRITE(V) if (!os::write(*m_stream, &(V), sizeof(V))) goto fail

			READ(in_header);
			if (in_header.magic != NetworkDebugHeader::MAGIC) goto fail;
			WRITE(header);
			gatherCallstack();
			return;

			#undef READ
			#undef WRITE

			fail:
				logError("Lua debugger network fail");
				os::close(*m_stream);
				m_stream = nullptr;
		}

		bool debugExecute(const char* cmd, OutputMemoryStream& blob){
			blob.clear();
			NetworkDebugType type = NetworkDebugType::EXECUTE;
			if (!os::write(*m_stream, &type, sizeof(type))) return false;
			if (!writeString(*m_stream, cmd)) return false;
			if (!os::read(*m_stream, &type, sizeof(type))) return false;
			if (type != NetworkDebugType::EXECUTE) return false;

			u32 size;
			if (!os::read(*m_stream, &size, sizeof(size))) return false;
			blob.resize(size);
			if (size > 0 && !os::read(*m_stream, blob.getMutableData(), size)) return false;
			return true;
		}

		static StringView toStringView(const OutputMemoryStream& blob) {
			StringView v;
			v.begin = (const char*)blob.data();
			v.end = v.begin + blob.size();
			return v;
		}

		void ui() {
			if (ImGui::Button(ICON_FA_UNLINK " Disconnect")) {
				os::close(*m_stream);
				m_stream = nullptr;
				return;
			}
			ImGui::SeparatorText("Eval");
			ImGui::SetNextItemWidth(-1);
			ImGui::InputTextMultiline("##code", m_eval_code, sizeof(m_eval_code));
			if (ImGui::Button(ICON_FA_PLAY "Run")) {
				if (debugExecute(m_eval_code, m_blob)) {
					m_eval_result = toStringView(m_blob);
				}
				else {
					os::close(*m_stream);
					m_stream = nullptr;
				}
			}
			if (m_eval_result.length() > 0) ImGui::TextUnformatted(m_eval_result.c_str());
			ImGui::SeparatorText("Callstack");
			ImGui::Columns(3);
			for (const CallstackItem& cs : m_callstack) {
				ImGui::PushID(&cs);
				const u32 idx = u32(&cs - m_callstack.begin());
				if (ImGui::Selectable(cs.source.c_str(), m_selected_callstack == idx, ImGuiSelectableFlags_SpanAllColumns)) {
					m_selected_callstack = idx;
					gatherLocals(idx);
				}
				ImGui::PopID();
				ImGui::NextColumn();
				ImGui::TextUnformatted(cs.name.c_str());
				ImGui::NextColumn();
				ImGui::Text("%d", cs.current_line);
				ImGui::NextColumn();
			}
			ImGui::Columns();
			ImGui::SeparatorText("Locals");
			for (const Local& local : m_locals) {
				ImGui::Text("%s: %s", local.name.c_str(), local.value.c_str());
			}
		}

		void gatherLocals(u32 level) {
			m_locals.clear();
			for (u32 i = 0; ; ++i) {
				StaticString<128> cmd("local name, val = debug.getlocal(", level + 3, ", ", i + 1, "); if not name then return \"\" end; return name");
				debugExecute(cmd, m_blob);
				if (m_blob.empty()) break;

				Local& local = m_locals.emplace(m_allocator);
				local.name = toStringView(m_blob);
				
				StaticString<128> cmd2("local name, val = debug.getlocal(", level + 3, ", ", i + 1, "); return tostring(val)");
				debugExecute(cmd2, m_blob);
				if (m_blob.empty()) break;

				local.value = toStringView(m_blob);
			}
		}

		void gatherCallstack() {
			m_callstack.clear();
			for (u32 i = 0; ; ++i) {
				StaticString<64> cmd("return debug.getinfo(", i + 3, ").source");
				debugExecute(cmd, m_blob);
				if (m_blob.empty()) break;

				CallstackItem& item = m_callstack.emplace(m_allocator);
				item.source = toStringView(m_blob);
				
				StaticString<64> cmd3("return debug.getinfo(", i + 3, ").name");
				debugExecute(cmd3, m_blob);
				if (m_blob.empty()) break;
				item.name = toStringView(m_blob);

				StaticString<64> cmd2("return debug.getinfo(", i + 3, ").currentline");
				if (!debugExecute(cmd2, m_blob)) break;
				fromCString(toStringView(m_blob), item.current_line);
			}
		}

		IAllocator& m_allocator;
		OutputMemoryStream m_blob;
		Array<CallstackItem> m_callstack;
		i32 m_selected_callstack = -1;
		Array<Local> m_locals;
		os::NetworkStream* m_stream = nullptr;
		String m_eval_result;
		char m_eval_code[1024] = "";
	};


]]