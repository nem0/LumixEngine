#pragma once

// see ../../docs/app_imgui.md for more info about this file
// #define LUMIX_APP_IMGUI_INTEGRATION

#include <imgui/imgui.h>
#include "core/array.h"
#include "core/os.h"
#include "core/path.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "renderer/gpu/gpu.h"
#include "renderer/renderer.h"

namespace Lumix {

#ifdef LUMIX_APP_IMGUI_INTEGRATION

struct ImGuiIntegration {
	ImGuiIntegration(IAllocator& allocator)
		: m_allocator(allocator)
		, m_textures(allocator)
	{}

	void endFrame() {
		ImGui::PopFont();
		ImGui::Render();
		Renderer* renderer = static_cast<Renderer*>(m_engine->getSystemManager().getSystem("renderer"));

		DrawStream& stream = renderer->getDrawStream();
		stream.beginProfileBlock("imgui", 0, true);

		u32 drawcalls = 0;
		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		for (const ImGuiViewport* vp : platform_io.Viewports) {
			ImDrawData* draw_data = vp->DrawData;
			if (!draw_data) continue;

			const u32 w = u32(vp->Size.x);
			const u32 h = u32(vp->Size.y);
			const Vec2 scale(2.f / w, -2.f / h);
			const Vec2 offset(-1 + (float)-draw_data->DisplayPos.x * 2.f / w, 1 + (float)draw_data->DisplayPos.y * 2.f / h); 

			if (!m_shader) {
				const char* src =
					R"#(struct VSInput {
							float2 pos : TEXCOORD0;
							float2 uv : TEXCOORD1;
							float4 color : TEXCOORD2;
						};

						cbuffer ImGuiState : register(b4) {
							float2 c_scale;
							float2 c_offset;
							uint c_texture;
							float c_time;
						};

						struct VSOutput {
							float4 color : TEXCOORD0;
							float2 uv : TEXCOORD1;
							float4 position : SV_POSITION;
						};

						VSOutput mainVS(VSInput input) {
							VSOutput output;
							output.color = input.color;
							output.uv = input.uv;
							float2 p = input.pos * c_scale + c_offset;
							output.position = float4(p.xy, 0, 1);
							return output;
						}

						float4 mainPS(VSOutput input) : SV_Target {
							float4 tc = sampleBindlessLod(LinearSamplerClamp, c_texture, input.uv, 0);
							return float4( 
								abs(tc.rgb) * pow(abs(input.color.rgb) /*to silence warning*/, (2.2).xxx),
								input.color.a * tc.a
							);
						}
					)#";
				gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLES);
				decl.addAttribute(0, 2, gpu::AttributeType::FLOAT, 0);
				decl.addAttribute(8, 2, gpu::AttributeType::FLOAT, 0);
				decl.addAttribute(16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
				const gpu::StateFlags state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				m_shader = gpu::allocProgramHandle();
				stream.createProgram(m_shader, state, decl, src, gpu::ShaderType::SURFACE, nullptr, 0, "imgui shader");
			}

			if (draw_data->Textures != nullptr) {
				for (ImTextureData* tex : *draw_data->Textures) {
					if (tex->Status != ImTextureStatus_OK) {
						updateTexture(*tex);
					}
				}
			}

			stream.setCurrentWindow(vp->PlatformHandle);
			stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
			stream.viewport(0, 0, w, h);
				
			for (int i = 0; i < draw_data->CmdListsCount; ++i) {
				drawcalls += draw_data->CmdLists[i]->CmdBuffer.size();
				encode(draw_data->CmdLists[i], vp, renderer, stream, m_shader, scale, offset);
			}
		}
		stream.setCurrentWindow(nullptr);
		stream.endProfileBlock();
	}

	void injectEvent(const os::Event& event) {
		ImGuiIO& io = ImGui::GetIO();
		switch (event.type) {
			case os::Event::Type::MOUSE_BUTTON:
				io.AddMouseButtonEvent((int)event.mouse_button.button, event.mouse_button.down);
				break;
			case os::Event::Type::MOUSE_WHEEL:
				io.AddMouseWheelEvent(0, event.mouse_wheel.amount);
				break;
			case os::Event::Type::KEY: {
				ImGuiKey key = m_key_map[(int)event.key.keycode];
				if (key != ImGuiKey_None) io.AddKeyEvent(key, event.key.down);
				break;
			}
			case os::Event::Type::CHAR: {
				char tmp[5] = {};
				memcpy(tmp, &event.text_input.utf8, sizeof(event.text_input.utf8));
				io.AddInputCharactersUTF8(tmp);
				break;
			}
			default:
				break;
		}

	}

	void beginFrame() {
		PROFILE_FUNCTION();

		ImGuiIO& io = ImGui::GetIO();

		const os::Point client_size = os::getWindowClientSize(m_engine->getMainWindow());
		if (client_size.x > 0 && client_size.y > 0) {
			io.DisplaySize = ImVec2(float(client_size.x), float(client_size.y));
		}
		else if(io.DisplaySize.x <= 0) {
			io.DisplaySize.x = 800;
			io.DisplaySize.y = 600;
		}
		io.DeltaTime = m_engine->getLastTimeDelta();

		const os::Point cp = os::getMouseScreenPos();
		const os::Rect screen_rect = os::getWindowScreenRect(m_engine->getMainWindow());
		io.AddMousePosEvent((float)cp.x - screen_rect.left, (float)cp.y - screen_rect.top);

		ImGui::NewFrame();
		ImGui::PushFont(m_font);
	}

	void updateTexture(ImTextureData& tex) {
		Renderer* renderer = static_cast<Renderer*>(m_engine->getSystemManager().getSystem("renderer"));

		switch (tex.Status) {
			case ImTextureStatus_Destroyed: break;
			case ImTextureStatus_OK: break;
			case ImTextureStatus_WantUpdates: {
					gpu::TextureHandle texture = (gpu::TextureHandle)(intptr_t)tex.GetTexID();
					DrawStream& draw_stream = renderer->getDrawStream();
					draw_stream.update(texture, 0, 0, 0, 0, tex.Width, tex.Height, gpu::TextureFormat::RGBA8, tex.GetPixels(), tex.Width * tex.Height * 4);
					tex.SetStatus(ImTextureStatus_OK);
				break;
			}
			case ImTextureStatus_WantDestroy: {
				DrawStream& draw_stream = renderer->getEndFrameDrawStream();
				gpu::TextureHandle texture = (gpu::TextureHandle)tex.GetTexID();
				draw_stream.destroy(texture);
				tex.SetStatus(ImTextureStatus_Destroyed);
				tex.SetTexID(ImTextureID_Invalid);
				m_textures.eraseItem(texture);
				break;
			}
			case ImTextureStatus_WantCreate: {
				ASSERT(tex.Format == ImTextureFormat_RGBA32);
			
				auto* pixels = (const u8*)tex.GetPixels();

				const Renderer::MemRef mem = renderer->copy(pixels, tex.Width * tex.Height * 4);
				gpu::TextureHandle texture = renderer->createTexture(tex.Width, tex.Height, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS, mem, "imgui_texture");
				m_textures.push(texture);
				tex.SetTexID((ImTextureID)(intptr_t)texture);
				tex.SetStatus(ImTextureStatus_OK);
				break;
			}
		}
	}

	struct ImGuiUniformBuffer {
		Vec2 scale;
		Vec2 offset;
		gpu::BindlessHandle texture_handle;
		float time;
	};

	void encode(const ImDrawList* cmd_list, const ImGuiViewport* vp, Renderer* renderer, DrawStream& stream, gpu::ProgramHandle program, Vec2 scale, Vec2 offset) {
		const TransientSlice ib = stream.allocTransient(cmd_list->IdxBuffer.size_in_bytes());
		memcpy(ib.ptr, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size_in_bytes());

		const TransientSlice vb  = stream.allocTransient(cmd_list->VtxBuffer.size_in_bytes());
		memcpy(vb.ptr, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size_in_bytes());

		stream.useProgram(program);
		stream.bindIndexBuffer(ib.buffer);
		stream.bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(ImDrawVert));
		stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

		for (int i = 0, c = cmd_list->CmdBuffer.size(); i < c; ++i) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[i];
			if (pcmd->UserCallback) {
				if (ImDrawCallback_ResetRenderState == pcmd->UserCallback) {
					stream.useProgram(program);
					stream.bindIndexBuffer(ib.buffer);
					stream.bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(ImDrawVert));
					stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
				}
				else {
					pcmd->UserCallback(cmd_list, pcmd);
				}
				continue;
			}
			if (0 == pcmd->ElemCount) continue;

			gpu::TextureHandle tex = (gpu::TextureHandle)(intptr_t)pcmd->GetTexID();
			if (tex) {
				const TransientSlice ub = stream->allocUniform(sizeof(ImGuiUniformBuffer));
				ImGuiUniformBuffer* uniform_data = (ImGuiUniformBuffer*)ub.ptr;
				uniform_data->scale = scale;
				uniform_data->offset = offset;
				uniform_data->texture_handle = gpu::getBindlessHandle(tex);
				static os::Timer timer;
				uniform_data->time = timer.getTimeSinceStart();
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);

				const u32 h = u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f));

				const ImVec2 pos = vp->DrawData->DisplayPos;
				const u32 vp_height = u32(vp->Size.y);
				if (gpu::isOriginBottomLeft()) {
					stream.scissor(u32(maximum((pcmd->ClipRect.x - pos.x), 0.0f)),
						vp_height - u32(maximum((pcmd->ClipRect.y - pos.y), 0.0f)) - h,
						u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x), 0.f, 65535.f)),
						u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f)));
				} else {
					stream.scissor(u32(maximum((pcmd->ClipRect.x - pos.x), 0.0f)),
						u32(maximum((pcmd->ClipRect.y - pos.y), 0.0f)),
						u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x), 0.f, 65535.f)),
						u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f)));
				}

				stream.drawIndexed(pcmd->IdxOffset * sizeof(u32) + ib.offset, pcmd->ElemCount, gpu::DataType::U32);
			}
		}
	}

	void init() {
		memset(m_key_map, 0, sizeof(m_key_map));
		m_key_map[(int)os::Keycode::CTRL] = ImGuiMod_Ctrl;
		m_key_map[(int)os::Keycode::ALT] = ImGuiMod_Alt;
		m_key_map[(int)os::Keycode::SHIFT] = ImGuiMod_Shift;
		m_key_map[(int)os::Keycode::LSHIFT] = ImGuiKey_LeftShift;
		m_key_map[(int)os::Keycode::RSHIFT] = ImGuiKey_RightShift;
		m_key_map[(int)os::Keycode::SPACE] = ImGuiKey_Space;
		m_key_map[(int)os::Keycode::TAB] = ImGuiKey_Tab;
		m_key_map[(int)os::Keycode::LEFT] = ImGuiKey_LeftArrow;
		m_key_map[(int)os::Keycode::RIGHT] = ImGuiKey_RightArrow;
		m_key_map[(int)os::Keycode::UP] = ImGuiKey_UpArrow;
		m_key_map[(int)os::Keycode::DOWN] = ImGuiKey_DownArrow;
		m_key_map[(int)os::Keycode::PAGEUP] = ImGuiKey_PageUp;
		m_key_map[(int)os::Keycode::PAGEDOWN] = ImGuiKey_PageDown;
		m_key_map[(int)os::Keycode::HOME] = ImGuiKey_Home;
		m_key_map[(int)os::Keycode::END] = ImGuiKey_End;
		m_key_map[(int)os::Keycode::DEL] = ImGuiKey_Delete;
		m_key_map[(int)os::Keycode::BACKSPACE] = ImGuiKey_Backspace;
		m_key_map[(int)os::Keycode::RETURN] = ImGuiKey_Enter;
		m_key_map[(int)os::Keycode::ESCAPE] = ImGuiKey_Escape;
		m_key_map[(int)os::Keycode::NUMPAD0] = ImGuiKey_Keypad0;
		m_key_map[(int)os::Keycode::NUMPAD1] = ImGuiKey_Keypad1;
		m_key_map[(int)os::Keycode::NUMPAD2] = ImGuiKey_Keypad2;
		m_key_map[(int)os::Keycode::NUMPAD3] = ImGuiKey_Keypad3;
		m_key_map[(int)os::Keycode::NUMPAD4] = ImGuiKey_Keypad4;
		m_key_map[(int)os::Keycode::NUMPAD5] = ImGuiKey_Keypad5;
		m_key_map[(int)os::Keycode::NUMPAD6] = ImGuiKey_Keypad6;
		m_key_map[(int)os::Keycode::NUMPAD7] = ImGuiKey_Keypad7;
		m_key_map[(int)os::Keycode::NUMPAD8] = ImGuiKey_Keypad8;
		m_key_map[(int)os::Keycode::NUMPAD9] = ImGuiKey_Keypad9;
		m_key_map[(int)os::Keycode::OEM_COMMA] = ImGuiKey_Comma;
		m_key_map[(int)os::Keycode::F1] = ImGuiKey_F1;
		m_key_map[(int)os::Keycode::F2] = ImGuiKey_F2;
		m_key_map[(int)os::Keycode::F3] = ImGuiKey_F3;
		m_key_map[(int)os::Keycode::F4] = ImGuiKey_F4;
		m_key_map[(int)os::Keycode::F5] = ImGuiKey_F5;
		m_key_map[(int)os::Keycode::F6] = ImGuiKey_F6;
		m_key_map[(int)os::Keycode::F7] = ImGuiKey_F7;
		m_key_map[(int)os::Keycode::F8] = ImGuiKey_F8;
		m_key_map[(int)os::Keycode::F9] = ImGuiKey_F9;
		m_key_map[(int)os::Keycode::F10] = ImGuiKey_F10;
		m_key_map[(int)os::Keycode::F11] = ImGuiKey_F11;
		m_key_map[(int)os::Keycode::F12] = ImGuiKey_F12;
		m_key_map['1'] = ImGuiKey_1;
		m_key_map['2'] = ImGuiKey_2;
		m_key_map['3'] = ImGuiKey_3;
		m_key_map['4'] = ImGuiKey_4;
		m_key_map['5'] = ImGuiKey_5;
		m_key_map['6'] = ImGuiKey_6;
		m_key_map['7'] = ImGuiKey_7;
		m_key_map['8'] = ImGuiKey_8;
		m_key_map['9'] = ImGuiKey_9;
		m_key_map['0'] = ImGuiKey_0;
		m_key_map['A'] = ImGuiKey_A;
		m_key_map['B'] = ImGuiKey_B;
		m_key_map['C'] = ImGuiKey_C;
		m_key_map['D'] = ImGuiKey_D;
		m_key_map['E'] = ImGuiKey_E;
		m_key_map['F'] = ImGuiKey_F;
		m_key_map['G'] = ImGuiKey_G;
		m_key_map['H'] = ImGuiKey_H;
		m_key_map['I'] = ImGuiKey_I;
		m_key_map['J'] = ImGuiKey_J;
		m_key_map['K'] = ImGuiKey_K;
		m_key_map['L'] = ImGuiKey_L;
		m_key_map['M'] = ImGuiKey_M;
		m_key_map['N'] = ImGuiKey_N;
		m_key_map['O'] = ImGuiKey_O;
		m_key_map['P'] = ImGuiKey_P;
		m_key_map['Q'] = ImGuiKey_Q;
		m_key_map['R'] = ImGuiKey_R;
		m_key_map['S'] = ImGuiKey_S;
		m_key_map['T'] = ImGuiKey_T;
		m_key_map['U'] = ImGuiKey_U;
		m_key_map['V'] = ImGuiKey_V;
		m_key_map['W'] = ImGuiKey_W;
		m_key_map['X'] = ImGuiKey_X;
		m_key_map['Y'] = ImGuiKey_Y;
		m_key_map['Z'] = ImGuiKey_Z;

		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendFlags = ImGuiBackendFlags_HasMouseCursors | ImGuiBackendFlags_RendererHasTextures;

		const int dpi = os::getDPI();
		float font_scale = dpi / 96.f;
		FileSystem& fs = m_engine->getFileSystem();
	
		m_font = addFontFromFile("editor/fonts/notosans-regular.ttf", 18.f);
	}

	ImFont* addFontFromFile(const char* path, float size) {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(Path(path), data)) return nullptr;
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		copyString(cfg.Name, path);
		cfg.FontDataOwnedByAtlas = false;
		auto font = io.Fonts->AddFontFromMemoryTTF((void*)data.data(), (i32)data.size(), size, &cfg);
		return font;
	}

	IAllocator& m_allocator;
	Engine* m_engine = nullptr;
	gpu::ProgramHandle m_shader = gpu::INVALID_PROGRAM;
	ImFont* m_font = nullptr;
	Array<gpu::TextureHandle> m_textures;
	ImGuiKey m_key_map[255];
};

#else

struct ImGuiIntegration {
	ImGuiIntegration(IAllocator&) {}
	void beginFrame() {}
	void endFrame() {}
	void injectEvent(const os::Event&) {}
	void init() {}

	Engine* m_engine = nullptr;
};

#endif

}