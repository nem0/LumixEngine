#if 1 // set to 0 to build minimal lunex example

#include "core/atomic.h"
#include "core/command_line_parser.h"
#include "core/debug.h"
#include "core/default_allocator.h"
#include "engine/file_system.h"
#include "core/geometry.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/path.h"
#include "core/profiler.h"
#include "core/thread.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/plugin.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/world.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "ui/ui_system.h"
#include "imgui_integration.h"
#include <stdlib.h>

// window title, set with premake's --app-title
#ifndef LUMIX_APP_TITLE
	#define LUMIX_APP_TITLE "Game"
#endif

#ifdef __linux__
	#define STB_IMAGE_IMPLEMENTATION
	#include "stb/stb_image.h"
#endif

using namespace Lumix;

struct Runner final
{
	Runner() 
		: m_allocator(m_main_allocator)
		, m_imgui(m_allocator)
	{
		debug::init(m_allocator);
		configureCrashReport(CrashReportFlags::ENABLED | CrashReportFlags::SENTRY);
		profiler::init(m_allocator);
		if (!jobs::init(os::getCPUsCount(), m_allocator)) {
			logError("Failed to initialize job system.");
		}
	}

	~Runner() {
		jobs::shutdown();
		profiler::shutdown();
		debug::shutdown();
		ASSERT(!m_world); 
	}

	void onResize() {
		if (!m_engine.get()) return;
		if (m_engine->getMainWindow() == os::INVALID_WINDOW) return;

		const os::Point client_size = os::getWindowClientSize(m_engine->getMainWindow());
		m_viewport.w = client_size.x;
		m_viewport.h = client_size.y;
	}

	void initRenderPipeline() {
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.far = 10'000.f;
		m_viewport.is_ortho = false;
		m_viewport.near = 0.1f;
		m_viewport.pos = {0, 0, 0};
		m_viewport.rot = Quat::IDENTITY;

		m_renderer = static_cast<Renderer*>(m_engine->getSystemManager().getSystem("renderer"));
		m_pipeline = Pipeline::create(*m_renderer, PipelineType::GAME_VIEW);

		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(100);
			m_engine->getFileSystem().processCallbacks();
		}

		m_pipeline->setWorld(m_world);
	}

	bool loadWorld() {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (!fs.getContentSync(m_startup_world, data)) return false;

		InputMemoryStream blob(data);
		EntityMap entity_map(m_allocator);

		WorldVersion editor_version;
		if (!m_world->deserialize(blob, entity_map, editor_version)) {
			logError("Failed to deserialize ", m_startup_world);
			return false;
		}
		copyString(m_world->getPartitions()[0].name, m_startup_world); 
		return true;
	}

	// Options for testing, they do nothing if they are not on the command line:
	//   -screenshot <absolute path>   saves a frame as a PNG and quits, after -screenshot_frame <n> frames (300 by default)
	//   -cam <x> <y> <z> <yaw> <pitch>   the camera of the view (angles in degrees) instead of the one of the world
	void parseTestOptions() {
		char cmd_line[4096] = "";
		if (!os::getCommandLine(Span(cmd_line))) return;
		CommandLineParser parser(cmd_line);
		auto next_number = [&](double& value) {
			char tmp[64];
			if (!parser.next()) return false;
			parser.getCurrent(tmp, lengthOf(tmp));
			value = atof(tmp);
			return true;
		};
		while (parser.next()) {
			if (parser.currentEquals("-screenshot")) {
				if (!parser.next()) break;
				parser.getCurrent(m_test_screenshot_path, lengthOf(m_test_screenshot_path));
			}
			else if (parser.currentEquals("-screenshot_frame")) {
				double frame;
				if (!next_number(frame)) break;
				m_test_screenshot_frame = (u32)frame;
			}
			else if (parser.currentEquals("-cam")) {
				double v[5];
				bool ok = true;
				for (double& value : v) ok = ok && next_number(value);
				if (!ok) break;
				m_test_cam_pos = DVec3(v[0], v[1], v[2]);
				m_test_cam_yaw = degreesToRadians((float)v[3]);
				m_test_cam_pitch = degreesToRadians((float)v[4]);
				m_test_cam = true;
			}
		}
	}

	void loadProject() {
		FileSystem& fs = m_engine->getFileSystem();
		OutputMemoryStream data(m_allocator);
		if (fs.getContentSync(Path("lumix.prj"), data)) {
			InputMemoryStream tmp(data);
			if (m_engine->deserializeProject(tmp) != DeserializeProjectResult::SUCCESS) {
				logError("Failed to deserialize project file");
			}
		}

		char cmd_line[4096];
		if (os::getCommandLine(cmd_line)) {
			CommandLineParser parser(cmd_line);
			while (parser.next()) {
				if (!parser.currentEquals("-world")) continue;
				if (!parser.next()) break;

				char src[MAX_PATH];
				parser.getCurrent(src, lengthOf(src));
				m_startup_world = src;
				break;
			}
		}
	}

	void onInit() {
		Engine::InitArgs init_data;
		if (os::dirExists("../data")) init_data.engine_data_dir = "../data";
		char data_dir[MAX_PATH] = "";
		char cmd_line[2048] = "";
		if (os::getCommandLine(Span(cmd_line))) {
			CommandLineParser parser(cmd_line);
			while (parser.next()) {
				if (parser.currentEquals("--data_dir") || parser.currentEquals("-data_dir")) {
					if (!parser.next()) break;
					parser.getCurrent(data_dir, lengthOf(data_dir));
				}
			}
		}

		if (os::fileExists("main.pak")) {
			init_data.file_system = FileSystem::createPacked("main.pak", m_allocator);
		}
		else {
			char current_dir[MAX_PATH];
			os::getCurrentDirectory(Span(current_dir));
			init_data.file_system = FileSystem::create(init_data.engine_data_dir ? init_data.engine_data_dir : current_dir, m_allocator);
		}

		// log is opened inside Engine::create, so the exe dir must be mounted before that
		char exe_path[MAX_PATH];
		os::getExecutablePath(Span(exe_path));
		const StringView exe_dir = Path::getDir(exe_path);
		init_data.file_system->mount(exe_dir, "exe");
		init_data.log_path = "exe/lumix_app.log";

		m_engine = Engine::create(static_cast<Engine::InitArgs&&>(init_data), m_allocator);
		if (!data_dir[0]) os::getCurrentDirectory(Span(data_dir));
		m_engine->getFileSystem().mount(data_dir, "");
		m_imgui.m_engine = m_engine.get();

		os::InitWindowArgs init_window_args;
		init_window_args.name = LUMIX_APP_TITLE;
		m_window = os::createWindow(init_window_args);
		m_engine->setMainWindow(m_window);

		m_engine->init();
		
		if (!CommandLineParser::isOn("-window")) {
			os::setFullscreen(m_engine->getMainWindow());
			captureMouse(true);
		}

		m_world = &m_engine->createWorld();
		parseTestOptions();
		initRenderPipeline();
		
		loadProject();

		if (!m_startup_world.isEmpty()) loadWorld();
		
		os::showCursor(false);
		while (m_engine->getFileSystem().hasWork()) {
			os::sleep(10);
			m_engine->getFileSystem().processCallbacks();
		}
		m_engine->getFileSystem().processCallbacks();

		onResize();
		m_engine->startGame(*m_world);

		os::showWindow(m_window);
		m_imgui.init();
	}

	void shutdown() {
		m_engine->destroyWorld(*m_world);
		m_pipeline.reset();
		m_engine.reset();
		m_world = nullptr;
	}

	void captureMouse(bool capture) {
		m_mouse_captured = capture;
		if (m_focused) {
			os::Rect r = os::getWindowScreenRect(m_engine->getMainWindow());
			os::clipCursor(m_engine->getMainWindow(), r);
		}
		else {
			os::clipCursor(os::INVALID_WINDOW, {});
		}
	}

	void onEvent(const os::Event& event) {
		m_imgui.injectEvent(event);

		if (m_engine.get()) {
			const bool is_mouse_up = event.type == os::Event::Type::MOUSE_BUTTON && !event.mouse_button.down;
			const bool is_key_up = event.type == os::Event::Type::KEY && !event.key.down;
			if (m_focused || is_mouse_up || is_key_up) {
				InputSystem& input = m_engine->getInputSystem();
				os::Point p = os::clientToScreen(m_window, 0, 0);
				const os::Point cursor = os::getMouseScreenPos();
				input.injectEvent(event, Vec2((float)(cursor.x - p.x), (float)(cursor.y - p.y)));
			}
		}
		switch (event.type) {
			case os::Event::Type::FOCUS:
				m_focused = event.focus.gained;
				captureMouse(m_focused);
				break;
			case os::Event::Type::QUIT:
			case os::Event::Type::WINDOW_CLOSE: 
				m_finished = true;
				break;
			case os::Event::Type::WINDOW_MOVE:
			case os::Event::Type::WINDOW_SIZE:
				onResize();
				captureMouse(m_focused);
				break;
			default: break;
		}
	}


	void onIdle() {
		if (m_mouse_captured) {
			os::Rect r = os::getWindowScreenRect(m_engine->getMainWindow());
			os::clipCursor(m_engine->getMainWindow(), r);
		}

		const UISystem* ui_system = (const UISystem*)m_engine->getSystemManager().getSystem("ui");
		const bool is_ingame_cursor = ui_system && ui_system->isCursorEnabled();
		if (m_is_ingame_cursor != is_ingame_cursor) {
			m_is_ingame_cursor = is_ingame_cursor;
			os::showCursor(m_is_ingame_cursor);
		}

		m_imgui.beginFrame();
		m_engine->update(*m_world);

		EntityPtr camera = m_pipeline->getModule()->getActiveCamera();
		if (camera.isValid()) {
			int w = m_viewport.w;
			int h = m_viewport.h;
			m_viewport = m_pipeline->getModule()->getCameraViewport((EntityRef)camera);
			m_viewport.w = w;
			m_viewport.h = h;
			// camera.getRay (scripts picking objects under the cursor) needs the screen size, GameView sets it in the editor
			m_pipeline->getModule()->setCameraScreenSize((EntityRef)camera, w, h);
		}

		if (m_test_cam) {
			m_viewport.pos = m_test_cam_pos;
			m_viewport.rot = Quat(Vec3(0, 1, 0), m_test_cam_yaw) * Quat(Vec3(1, 0, 0), m_test_cam_pitch);
		}

		// the screenshot is saved by the pipeline at the end of the frame that was requested in
		++m_test_frame;
		if (m_test_screenshot_path[0] && !m_test_screenshot_requested && m_test_frame >= m_test_screenshot_frame) {
			m_test_screenshot_requested = m_renderer->requestScreenshot(m_test_screenshot_path, 0, 0);
			if (!m_test_screenshot_requested) m_finished = true;
		}

		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_pipeline->blitOutputToScreen();
		m_imgui.endFrame();
		m_renderer->frame();

		if (m_test_screenshot_requested) {
			const ScreenshotState state = m_renderer->getScreenshotState();
			if (state == ScreenshotState::SAVED || state == ScreenshotState::FAILED || m_test_frame > m_test_screenshot_frame + 600) m_finished = true;
		}
	}

	DefaultAllocator m_main_allocator;
	debug::Allocator m_allocator;
	UniquePtr<Engine> m_engine;
	Renderer* m_renderer = nullptr;
	World* m_world = nullptr;
	UniquePtr<Pipeline> m_pipeline;
	Path m_startup_world;
	os::WindowHandle m_window = os::INVALID_WINDOW;

	Viewport m_viewport;
	bool m_finished = false;
	bool m_focused = true;
	bool m_mouse_captured = false;
	bool m_is_ingame_cursor = false;

	// see parseTestOptions
	char m_test_screenshot_path[MAX_PATH] = "";
	u32 m_test_screenshot_frame = 300;
	u32 m_test_frame = 0;
	bool m_test_screenshot_requested = false;
	bool m_test_cam = false;
	DVec3 m_test_cam_pos = DVec3(0, 0, 0);
	float m_test_cam_yaw = 0;
	float m_test_cam_pitch = 0;

	ImGuiIntegration m_imgui;
};


int main(int args, char* argv[])
{
	struct Data {
		Data() : semaphore(0, 1) {}
		Runner app;
		Semaphore semaphore;
		int exit_code = 0;
	} data;
	
	profiler::setThreadName("Main thread");
	jobs::run(&data, [](void* ptr) {
		Data* data = (Data*)ptr;

		data->app.onInit();
		while(!data->app.m_finished) {
			os::Event e;
			while(os::getEvent(e)) {
				data->app.onEvent(e);
			}
			data->app.onIdle();
			if (data->app.m_engine->isQuitRequested()) {
				data->exit_code = data->app.m_engine->getQuitCode();
				data->app.m_finished = true;
			}
		}

		data->app.shutdown();

		data->semaphore.signal();
	}, nullptr, 0);
	
	PROFILE_BLOCK("sleeping");
	data.semaphore.wait();

	return data.exit_code;
}

#else

#include "core/debug.h"
#include "core/default_allocator.h"
#include "core/job_system.h"
#include "core/log_callback.h"
#include "core/os.h"
#include "core/profiler.h"
#include "renderer/gpu/gpu.h"

using namespace Lumix;

static void logToDebugOutput(LogLevel level, const char* message)
{
	if (level == LogLevel::ERROR) {
		debug::debugOutput("Error: ");
	}
	debug::debugOutput(message);
	debug::debugOutput("\n");
}

int main(int args, char* argv[]) {
	registerLogCallback<logToDebugOutput>();
	os::init();
	DefaultAllocator allocator;
	debug::init(allocator);
	profiler::init(allocator);
	jobs::init(os::getCPUsCount(), allocator);
	// create window
	os::WindowHandle win = os::createWindow({ .width = 640, .height = 480 });

	// init GPU
	gpu::preinit(allocator, true);
	gpu::init(win, gpu::InitFlags::NONE);
	gpu::ProgramHandle shader = gpu::allocProgramHandle();

	// texture
	gpu::TextureHandle texture = gpu::allocTextureHandle();
	gpu::createTexture(texture, 2, 2, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS, "checkerboard");
	u32 texels[] = {
		0xffFFffFF, 0,
		0, 0xffFFffFF
	};
	gpu::update(texture, 0, 0, 0, 0, 2, 2, gpu::TextureFormat::RGBA8, texels, sizeof(texels));

	// create shader
	const char* src = 
		R"#(
			SamplerState NearestSampler : register(s3);
			Texture2D<float4> bindless_textures[] : register(t0, space1);

			struct VSOutput {
				float2 uv : TEXCOORD1;
				float4 position : SV_POSITION;
			};
			
			VSOutput mainVS(uint vertex_id : SV_VertexID) {
				VSOutput output;
				output.uv = 10 * float2(vertex_id & 1, (vertex_id >> 1) & 1);
				output.position = float4(vertex_id & 1, (vertex_id >> 1) & 1, 0, 1);
				return output;
			}

			float4 mainPS(VSOutput input) : SV_Target {
				return bindless_textures[2052].Sample(NearestSampler, input.uv);
			}
		)#";
	gpu::createProgram(shader, gpu::StateFlags::NONE, gpu::PrimitiveType::TRIANGLES, src, gpu::ShaderType::SURFACE, "shader");

	// main loop
	bool finished = false;
	while (!finished) {
		// process OS events
		os::Event e;
		while (os::getEvent(e)) {
			switch (e.type) {
				case os::Event::Type::WINDOW_CLOSE:
				case os::Event::Type::QUIT: finished = true; break;
				default: break;
			}
		}

		// draw
		os::Point size = os::getWindowClientSize(win);
		gpu::viewport(0, 0, size.x, size.y);
		gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
		const float clear_col[] = {0.1f, 0.1f, 0.1f, 1};
		gpu::clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, clear_col, 0);
		gpu::useProgram(shader);
		gpu::drawArrays(0, 3);

		u32 frame = gpu::present();
		gpu::waitFrame(frame);
	}

	// shutdown
	gpu::shutdown();

	os::destroyWindow(win);
}

#endif
