#include "lumscript/lumscript_module.h"
#include "engine/world.h"
#include "engine/engine.h"
#include "engine/input_system.h"
#include "engine/resource_manager.h"
#include "engine/file_system.h"
#include "core/allocator.h"
#include "core/array.h"
#include "core/stream.h"
#include "core/log.h"
#include "core/tag_allocator.h"
#include "core/path.h"
#include "lumscript/lumscript_resource.h"
#include "lumscript/lumscript.h"
#include "lumscript/lumscript_engine_api.h"

namespace Lumix {

using namespace LumScript;

struct LumScriptModuleImpl : LumScriptModule {
	struct Script {
		Path path;
		Module* module = nullptr;
		Runtime* runtime = nullptr;
		LumScriptResource* resource = nullptr;
		bool is_ready = false;
	};

	LumScriptModuleImpl(World& world, ISystem& system)
		: m_world(world)
		, m_system(system)
		, m_allocator(world.getAllocator())
	{
	}

	~LumScriptModuleImpl() {
		if (m_script.runtime) {
			LUMIX_DELETE(m_allocator, m_script.runtime);
			m_script.runtime = nullptr;
		}
		if (m_script.module) {
			LUMIX_DELETE(m_allocator, m_script.module);
			m_script.module = nullptr;
		}
		if (m_script.resource) {
			m_script.resource->getObserverCb().unbind<&LumScriptModuleImpl::onResourceChanged>(this);
			m_script.resource->decRefCount();
			m_script.resource = nullptr;
		}
	}

	const char* getName() const override { return "lumscript"; }
	World& getWorld() override { return m_world; }
	ISystem& getSystem() const override { return m_system; }

	void serialize(OutputMemoryStream& serializer) override {
		// Serialize world-level script data
		serializer.writeString(m_script.path.c_str());
	}

	void deserialize(InputMemoryStream& deserializer, const EntityMap& entity_map, i32 version) override {
		// Deserialize world-level script data
		const char* path = deserializer.readString();
		if (path && path[0]) {
			load(Path(path));
		}
	}

	void update(float time_delta) override {
		if (!m_script.is_ready || !m_script.runtime) return;
		
		// Call update function for world-level script
		Value result;
		Diagnostics diag(m_allocator);
		Value dt_value = Runtime::makeF32(time_delta);
		m_script.runtime->call("update", Span<const Value>(&dt_value, 1), &result, diag);
		
		if (diag.has_error) {
			logError("LumScript update: ", diag.message);
		}
	}

	void load(const Path& path) override {
		// Unload old resource
		if (m_script.resource) {
			m_script.resource->getObserverCb().unbind<&LumScriptModuleImpl::onResourceChanged>(this);
			m_script.resource->decRefCount();
			m_script.resource = nullptr;
		}
		if (m_script.runtime) {
			LUMIX_DELETE(m_allocator, m_script.runtime);
			m_script.runtime = nullptr;
		}
		if (m_script.module) {
			LUMIX_DELETE(m_allocator, m_script.module);
			m_script.module = nullptr;
		}

		m_script.path = path;
		m_script.is_ready = false;

		// Load new resource
		if (!path.isEmpty()) {
			LumScriptSystem* lua_system = static_cast<LumScriptSystem*>(&m_system);
			auto* res = lua_system->getEngine().getResourceManager().load<LumScriptResource>(path);
			m_script.resource = res;
			if (res) res->onLoaded<&LumScriptModuleImpl::onResourceChanged>(this);
		}
	}

	bool isReady() const override {
		return m_script.is_ready;
	}

	void startGame() override {
		// Auto-load {world_name}.lum
		Path lum_path { m_world.getPartitions()[0].name };
		if (lum_path.isEmpty()) return;

		char tmp[MAX_PATH];
		copyString(tmp, lum_path.c_str());
		Path::replaceExtension(tmp, "lum");
		lum_path = tmp;
		
		LumScriptSystem* lua_system = static_cast<LumScriptSystem*>(&m_system);
		if (!lua_system->getEngine().getFileSystem().fileExists(lum_path)) {
			load(Path());
			return;
		}
		load(lum_path);
	}

private:
	struct ImportContext {
		World* world;
		FileSystem* filesystem;
		IAllocator* allocator;
		Array<OutputMemoryStream> sources;

		ImportContext(World& world, FileSystem& filesystem, IAllocator& allocator)
			: world(&world)
			, filesystem(&filesystem)
			, allocator(&allocator)
			, sources(allocator)
		{}
	};

	static bool isValidCoreImportPath(StringView path) {
		if (!startsWith(path, "core:")) return false;
		StringView name = path.withoutLeft(5);
		if (name.empty() || name[0] == '/' || name[0] == '\\') return false;
		return !find(name, "..") && !find(name, ':') && !find(name, '\\');
	}

	static bool resolveImport(Module& module, StringView path, StringView alias, StringView* source, void* userdata) {
		ImportContext* ctx = (ImportContext*)userdata;
		if (resolveEngineImport(module, ctx->world, path, alias)) {
			*source = {};
			return true;
		}
		if (isValidCoreImportPath(path)) {
			StringView name = path.withoutLeft(5);
			const bool has_lum_extension = endsWith(name, ".lum");
			Path file_path = has_lum_extension ? Path("engine/scripts/core/", name) : Path("engine/scripts/core/", name, ".lum");
			OutputMemoryStream& blob = ctx->sources.emplace(*ctx->allocator);
			if (!ctx->filesystem->getContentSync(file_path, blob)) {
				ctx->sources.pop();
				return false;
			}
			*source = StringView((const char*)blob.data(), (u32)blob.size());
			return true;
		}
		return false;
	}

	void onResourceChanged(Resource::State, Resource::State new_state, Resource& resource) {
		if (&resource != m_script.resource) return;
		if (new_state == Resource::State::READY) {
			m_script.is_ready = compileAndRun();
		}
		else {
			m_script.is_ready = false;
		}
	}

	bool compileAndRun() {
		if (!m_script.resource) return false;

		Diagnostics diagnostics(m_allocator);
		
		// Parse and compile the world script
			if (m_script.module) {
				LUMIX_DELETE(m_allocator, m_script.module);
			}
			m_script.module = LUMIX_NEW(m_allocator, Module)(m_allocator);
			
			LumScriptSystem* lumscript_system = static_cast<LumScriptSystem*>(&m_system);
			ImportContext import_ctx(m_world, lumscript_system->getEngine().getFileSystem(), m_allocator);
			if (!compileWithBuiltins(*m_script.module, m_script.resource->getSourceCode(), diagnostics, &resolveImport, &import_ctx, m_script.path.c_str())) {
				logError("LumScript compilation failed: ", diagnostics.message);
				return false;
			}

			// Create runtime
			if (m_script.runtime) {
				LUMIX_DELETE(m_allocator, m_script.runtime);
			}
			m_script.runtime = LUMIX_NEW(m_allocator, Runtime)(*m_script.module, m_allocator);
			if (m_script.runtime->findFunction("init") >= 0) {
				Value world_value;
				world_value.type = TypeRef(TypeRef::NATIVE, "engine:world/World", -1);
				world_value.ptr = &m_world;
				Value input_value;
				input_value.type = TypeRef(TypeRef::NATIVE, "engine:input/InputSystem", -1);
				input_value.ptr = &static_cast<LumScriptSystem&>(m_system).getEngine().getInputSystem();
				Value args[] = { world_value, input_value };
				if (!m_script.runtime->call("init", Span<const Value>(args), nullptr, diagnostics)) {
					logError("LumScript init failed: ", diagnostics.message);
					return false;
				}
			}
			return true;
	}

	World& m_world;
	ISystem& m_system;
	IAllocator& m_allocator;
	Script m_script;
};

struct LumScriptSystemImpl : LumScriptSystem {
	explicit LumScriptSystemImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator(), "lumscript")
		, m_lumscript_resource_manager(m_allocator)
	{
		// Register the LumScript resource type
		m_lumscript_resource_manager.create(LumScriptResource::TYPE, m_engine.getResourceManager());
	}

	const char* getName() const override { return "lumscript_system"; }
	Engine& getEngine() override { return m_engine; }
	
	void serialize(OutputMemoryStream& serializer) const override {}
	bool deserialize(i32 version, InputMemoryStream& serializer) override { return true; }

	void createModules(World& world) override {
		auto module = UniquePtr<LumScriptModuleImpl>::create(m_allocator, world, *this);
		world.addModule(module.move());
	}

	void updateScripts(float time_delta) override {
		// Called if needed for script-level updates across all modules
	}

private:
	Engine& m_engine;
	TagAllocator m_allocator;
	LumScriptResourceManager m_lumscript_resource_manager;
};

IModule* createLumScriptModule(World& world);
void destroyLumScriptModule(IModule* module);

LUMIX_PLUGIN_ENTRY(lumscript) {
	return LUMIX_NEW(engine.getAllocator(), LumScriptSystemImpl)(engine);
}

} // namespace Lumix
