#include "physics/physics_system.h"

#include <PxPhysicsAPI.h>

#include "cooking/PxCooking.h"
#include "core/base_proxy_allocator.h"
#include "core/crc32.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "editor/world_editor.h"
#include "engine.h"
#include "physics/physics_geometry_manager.h"
#include "physics/physics_scene.h"
#include "renderer/render_scene.h"
#include "studio_lib/studio_app.h"
#include "studio_lib/utils.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32 BOX_ACTOR_HASH = crc32("box_rigid_actor");
static const uint32 MESH_ACTOR_HASH = crc32("mesh_rigid_actor");
static const uint32 CONTROLLER_HASH = crc32("physical_controller");
static const uint32 HEIGHTFIELD_HASH = crc32("physical_heightfield");



struct PhysicsSystemImpl : public PhysicsSystem
{
	PhysicsSystemImpl(Engine& engine)
		: m_allocator(engine.getAllocator())
		, m_engine(engine)
		, m_manager(*this, engine.getAllocator())
	{
		m_manager.create(ResourceManager::PHYSICS, engine.getResourceManager());
	}

	bool create() override;
	IScene* createScene(UniverseContext& universe) override;
	void destroyScene(IScene* scene) override;
	void destroy() override;

	physx::PxPhysics* getPhysics() override
	{
		return m_physics;
	}

	physx::PxCooking* getCooking() override
	{
		return m_cooking;
	}
	
	bool connect2VisualDebugger();

	physx::PxPhysics*			m_physics;
	physx::PxFoundation*		m_foundation;
	physx::PxControllerManager*	m_controller_manager;
	physx::PxAllocatorCallback*	m_physx_allocator;
	physx::PxErrorCallback*		m_error_callback;
	physx::PxCooking*			m_cooking;
	PhysicsGeometryManager		m_manager;
	class Engine&				m_engine;
	class BaseProxyAllocator	m_allocator;
};


extern "C" LUMIX_PHYSICS_API IPlugin* createPlugin(Engine& engine)
{
	return LUMIX_NEW(engine.getAllocator(), PhysicsSystemImpl)(engine);
}


struct EditorPlugin : public WorldEditor::Plugin
{
	EditorPlugin(WorldEditor& editor)
		: m_editor(editor)
	{
	}

	bool showGizmo(ComponentUID cmp) override
	{
		PhysicsScene* phy_scene = static_cast<PhysicsScene*>(cmp.scene);
		if (cmp.type == CONTROLLER_HASH)
		{
			auto* scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			float height = phy_scene->getControllerHeight(cmp.index);
			float radius = phy_scene->getControllerRadius(cmp.index);

			Universe& universe = scene->getUniverse();
			Vec3 pos = universe.getPosition(cmp.entity);
			scene->addDebugCapsule(pos, height, radius, 0xff0000ff, 0);
			return true;
		}

		if (cmp.type == BOX_ACTOR_HASH)
		{
			auto* scene = static_cast<RenderScene*>(m_editor.getScene(crc32("renderer")));
			Vec3 extents = phy_scene->getHalfExtents(cmp.index);

			Universe& universe = scene->getUniverse();
			Matrix mtx = universe.getMatrix(cmp.entity);

			scene->addDebugCube(mtx.getTranslation(),
				mtx.getXVector() * extents.x,
				mtx.getYVector() * extents.y,
				mtx.getZVector() * extents.z,
				0xffff0000,
				0);
			return true;

		}

		return false;
	}

	WorldEditor& m_editor;
};



struct CustomErrorCallback : public physx::PxErrorCallback
{
	virtual void reportError(physx::PxErrorCode::Enum code, const char* message, const char* file, int line);
};

IScene* PhysicsSystemImpl::createScene(UniverseContext& ctx)
{
	return PhysicsScene::create(*this, ctx, m_engine, m_allocator);
}


void PhysicsSystemImpl::destroyScene(IScene* scene)
{
	PhysicsScene::destroy(static_cast<PhysicsScene*>(scene));
}


class AssertNullAllocator : public physx::PxAllocatorCallback
{
	public:
		void* allocate(size_t size, const char*, const char*, int) override
		{
			void* ret = _aligned_malloc(size, 16);
			// g_log_info.log("PhysX") << "Allocated " << size << " bytes for " << typeName << "
			// from " << filename << "(" << line << ")";
			ASSERT(ret);
			return ret;
		}
		void deallocate(void* ptr) override
		{
			_aligned_free(ptr);
		}
};


bool PhysicsSystemImpl::create()
{
	m_physx_allocator = LUMIX_NEW(m_allocator, AssertNullAllocator);
	m_error_callback = LUMIX_NEW(m_allocator, CustomErrorCallback);
	m_foundation = PxCreateFoundation(
		PX_PHYSICS_VERSION,
		*m_physx_allocator,
		*m_error_callback
	);

	m_physics = PxCreatePhysics(
		PX_PHYSICS_VERSION,
		*m_foundation,
		physx::PxTolerancesScale()
	);
	
	physx::PxTolerancesScale scale;
	m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_foundation, physx::PxCookingParams(scale));
	connect2VisualDebugger();
	return true;
}


void PhysicsSystemImpl::destroy()
{
	m_cooking->release();
	m_physics->release();
	m_foundation->release();
	LUMIX_DELETE(m_allocator, m_physx_allocator);
	LUMIX_DELETE(m_allocator, m_error_callback);
}


bool PhysicsSystemImpl::connect2VisualDebugger()
{
	if(m_physics->getPvdConnectionManager() == nullptr)
		return false;

	const char* pvd_host_ip = "127.0.0.1";
	int port = 5425;
	unsigned int timeout = 100; 
	physx::PxVisualDebuggerConnectionFlags connectionFlags = physx::PxVisualDebuggerExt::getAllConnectionFlags();

	auto* theConnection = physx::PxVisualDebuggerExt::createConnection(m_physics->getPvdConnectionManager(), pvd_host_ip, port, timeout, connectionFlags);
	return theConnection != nullptr;
}


void CustomErrorCallback::reportError(physx::PxErrorCode::Enum,
	const char* message,
	const char*,
	int)
{
	g_log_error.log("PhysX") << message;
}


extern "C" LUMIX_PHYSICS_API void setStudioApp(StudioApp& app)
{
	struct Plugin : public StudioApp::IPlugin
	{
		Plugin(Lumix::WorldEditor& editor)
			: m_editor(editor)
		{
			m_action = LUMIX_NEW(m_editor.getAllocator(), Action)("Physics", "physics");
			m_action->func.bind<Plugin, &Plugin::onAction>(this);
			m_is_window_opened = false;
		}


		void onAction()
		{
			m_is_window_opened = !m_is_window_opened;
		}


		void onWindowGUI() override
		{
			auto* scene = static_cast<PhysicsScene*>(m_editor.getScene(crc32("physics")));
			if (ImGui::BeginDock("Physics", &m_is_window_opened))
			{
				if (ImGui::CollapsingHeader("Layers"))
				{
					for (int i = 0; i < scene->getCollisionsLayersCount(); ++i)
					{
						char buf[30];
						copyString(buf, scene->getCollisionLayerName(i));
						char label[10];
						toCString(i, label, lengthOf(label));
						if (ImGui::InputText(label, buf, lengthOf(buf)))
						{
							scene->setCollisionLayerName(i, buf);
						}
					}
					if (ImGui::Button("Add layer"))
					{
						scene->addCollisionLayer();
					}
					if (scene->getCollisionsLayersCount() > 1)
					{
						ImGui::SameLine();
						if (ImGui::Button("Remove layer"))
						{
							scene->removeCollisionLayer();
						}
					}
				}

				if (ImGui::CollapsingHeader("Collision matrix", nullptr, true, true))
				{
					ImGui::Columns(1 + scene->getCollisionsLayersCount());
					ImGui::NextColumn();
					ImGui::PushTextWrapPos(1);
					float basic_offset = 0;
					for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
					{
						auto* layer_name = scene->getCollisionLayerName(i);
						basic_offset = Math::maxValue(basic_offset, ImGui::CalcTextSize(layer_name).x);
					}
					basic_offset += ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().WindowPadding.x;

					for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
					{
						auto* layer_name = scene->getCollisionLayerName(i);
						float offset = basic_offset + i * 35.0f;
						ImGui::SetColumnOffset(-1, offset);
						ImGui::Text(layer_name);
						ImGui::NextColumn();
					}
					ImGui::PopTextWrapPos();

					ImGui::Separator();
					for (int i = 0, c = scene->getCollisionsLayersCount(); i < c; ++i)
					{
						ImGui::Text(scene->getCollisionLayerName(i));
						ImGui::NextColumn();

						for (int j = 0; j <= i; ++j)
						{
							bool b = scene->canLayersCollide(i, j);
							if (ImGui::Checkbox(StringBuilder<10>("###", i, "-") << j, &b))
							{
								scene->setLayersCanCollide(i, j, b);
							}
							ImGui::NextColumn();
						}
						for (int j = i + 1; j < c; ++j)
						{
							ImGui::NextColumn();
						}
					}
					ImGui::Columns();
				}
			}

			ImGui::EndDock();
		}


		const char* getWindowName() override { return "Physics"; }


		bool m_is_window_opened;
		Lumix::WorldEditor& m_editor;
	};

	Plugin* plugin = LUMIX_NEW(app.getWorldEditor()->getAllocator(), Plugin)(*app.getWorldEditor());
	app.addPlugin(*plugin);
}


extern "C" LUMIX_PHYSICS_API void setWorldEditor(Lumix::WorldEditor& editor)
{
	auto* plugin = LUMIX_NEW(editor.getAllocator(), EditorPlugin)(editor);
	editor.addPlugin(*plugin);
}


} // !namespace Lumix



