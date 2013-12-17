#include "renderer.h"
#include <cmath>
#include <cstdio>
#include <Windows.h>
#include "Horde3DUtils.h"
#include "core/crc32.h"
#include "core/file_system.h"
#include "core/ifile.h"
#include "core/ifile_system_defines.h"
#include "core/json_serializer.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/vec3.h"
#include "gl/GL.h"
#include "universe/component_event.h"
#include "universe/entity_destroyed_event.h"
#include "universe/entity_moved_event.h"
#include "universe/universe.h"


#pragma comment(lib, "Horde3D.lib")
#pragma comment(lib, "Horde3DUtils.lib")
#pragma comment(lib, "opengl32.lib")
#define GL_TEXTURE0                       0x84C0



namespace Lux
{

typedef void (APIENTRY * glActiveTextureEXT_func)(const GLenum prg);
typedef void (APIENTRY * glUseProgramEXT_func)(const GLuint prg);

static glUseProgramEXT_func glUseProgramEXT = NULL;
static glActiveTextureEXT_func glActiveTextureEXT = NULL;
static const Component::Type rend_type = crc32("renderable");
static const Component::Type point_light_type = crc32("point_light");


struct RenderNode
{
	RenderNode() {}
	H3DNode m_node;
	int m_entity;
};


struct RendererImpl
{
	void loadResources();
	void postDeserialize();
	void onEvent(Event& event);
	void onResize(int w, int h);
	void destroyPointLight(Component cmp);
	void destroyRenderable(Component cmp);

	int					m_width;
	int					m_height;
	vector<RenderNode>	m_lights;
	vector<RenderNode>	m_renderables;
	vector<string>		m_paths;
	vector<Entity>		m_entities;
	H3DRes				m_pipeline_handle;
	Universe*			m_universe;
	H3DNode				m_camera_node;
	int					m_first_free_renderable;
	int					m_first_free_light;
	string				m_base_path;
	FS::FileSystem*		m_file_system;
	H3DRes				m_loading_res;
	Renderer*			m_owner;
	bool				m_update_bb;
	bool				m_is_pipeline_loaded;
};


Renderer::Renderer()
{
	m_impl = 0;
}


void Renderer::onResize(int w, int h)
{
	m_impl->onResize(w, h);
}


void RendererImpl::onResize(int w, int h)
{
	m_width = w;
	m_height = h;
	h3dResizePipelineBuffers(m_pipeline_handle, m_width, m_height);
	h3dSetNodeParamI(m_camera_node, H3DCamera::ViewportXI, 0);
	h3dSetNodeParamI(m_camera_node, H3DCamera::ViewportYI, 0);
	h3dSetNodeParamI(m_camera_node, H3DCamera::ViewportWidthI, m_width);
	h3dSetNodeParamI(m_camera_node, H3DCamera::ViewportHeightI, m_height);
	h3dSetupCameraView(m_camera_node, 45.0f, (float)m_width / m_height, 0.1f, 1000.0f);
}


void resourceLoaded(void* user_data, char* file_data, int length, bool success)
{
	RendererImpl* renderer = static_cast<RendererImpl*>(user_data);
	if(success)
	{
		renderer->m_update_bb = true;
		h3dLoadResource(renderer->m_loading_res, file_data, length);
		if(renderer->m_loading_res == renderer->m_pipeline_handle)
		{
			h3dResizePipelineBuffers(renderer->m_pipeline_handle, renderer->m_width, renderer->m_height);
	//		h3dSetOption( H3DOptions::DebugViewMode, 1 );
			h3dSetOption( H3DOptions::LoadTextures, 1 );
			h3dSetOption( H3DOptions::TexCompression, 0 );
			h3dSetOption( H3DOptions::FastAnimation, 0 );
			h3dSetOption( H3DOptions::MaxAnisotropy, 4 );
			h3dSetOption( H3DOptions::ShadowMapSize, 512 );

			renderer->m_camera_node = h3dAddCameraNode(H3DRootNode, "", renderer->m_pipeline_handle);
			renderer->onResize(renderer->m_width, renderer->m_height);
			renderer->m_is_pipeline_loaded = false;
		}
		renderer->m_loading_res = 0;
		renderer->loadResources();
	}
	else
	{
		h3dLoadResource(renderer->m_loading_res, 0, 0);
	}
}

void resourceLoaded(FS::IFile* file, bool success, void* user_data)
{
	RendererImpl* renderer = static_cast<RendererImpl*>(user_data);
	if(success)
	{
		renderer->m_update_bb = true;
		h3dLoadResource(renderer->m_loading_res, (const char*)file->getBuffer(), file->size());
		if(renderer->m_loading_res == renderer->m_pipeline_handle)
		{
			h3dResizePipelineBuffers(renderer->m_pipeline_handle, renderer->m_width, renderer->m_height);
	//		h3dSetOption( H3DOptions::DebugViewMode, 1 );
			h3dSetOption( H3DOptions::LoadTextures, 1 );
			h3dSetOption( H3DOptions::TexCompression, 0 );
			h3dSetOption( H3DOptions::FastAnimation, 0 );
			h3dSetOption( H3DOptions::MaxAnisotropy, 4 );
			h3dSetOption( H3DOptions::ShadowMapSize, 512 );

			renderer->m_camera_node = h3dAddCameraNode(H3DRootNode, "", renderer->m_pipeline_handle);
			renderer->onResize(renderer->m_width, renderer->m_height);
		}
		renderer->m_loading_res = 0;
		renderer->loadResources();
	}
	else
	{
		h3dLoadResource(renderer->m_loading_res, 0, 0);
	}
}


void Renderer::enableStage(const char* name, bool enable)
{
	int i = 0;
	while(true)
	{
		const char* n = h3dGetResParamStr(m_impl->m_pipeline_handle, H3DPipeRes::StageElem, i, H3DPipeRes::StageNameStr);
		if(strcmp(n, name) == 0)
		{
			h3dSetResParamI(m_impl->m_pipeline_handle, H3DPipeRes::StageElem, i, H3DPipeRes::StageActivationI, enable ? 1 : 0);
			return;
		}
		if(n == 0 || n[0] == '\0')
		{
			return;
		}
		++i;
	}
}


void RendererImpl::loadResources()
{
	H3DRes res = h3dQueryUnloadedResource(0);
	char path[255]; /// TODO 255 -> platform constant
	if(res != 0 && res != m_loading_res)
	{
		m_loading_res = res;
		sprintf_s(path, "%s%s/%s", m_base_path.c_str(), h3dutGetResourcePath(h3dGetResType(res)), h3dGetResName(res));
		m_file_system->openAsync(m_file_system->getDefaultDevice(), path, FS::Mode::OPEN | FS::Mode::READ, &resourceLoaded, this);
	}
}


bool Renderer::create(FS::FileSystem* fs, int w, int h, const char* base_path)
{
	m_impl = new RendererImpl();
	m_impl->m_pipeline_handle = -1;
	m_impl->m_owner = this;
	m_impl->m_file_system = fs;
	m_impl->m_first_free_renderable = -1;
	m_impl->m_first_free_light = -1;
	m_impl->m_width = -1;
	m_impl->m_height = -1;
	m_impl->m_universe = 0;
	m_impl->m_camera_node = 0;
	m_impl->m_is_pipeline_loaded = false;
	m_impl->m_base_path = base_path;
	m_impl->m_width = w;
	m_impl->m_height = h;
	
	if(!h3dInit())
	{	
		delete m_impl;
		m_impl = 0;
		h3dutDumpMessages();
		return false;
	}
	m_impl->m_pipeline_handle = h3dAddResource(H3DResTypes::Pipeline, "pipelines/forward.pipeline.xml", 0);
	m_impl->loadResources();
	glUseProgramEXT = (glUseProgramEXT_func)wglGetProcAddress("glUseProgram");
	glActiveTextureEXT = (glActiveTextureEXT_func)wglGetProcAddress("glActiveTexture");
	return true;
}


void Renderer::destroy()
{
	h3dRelease();
	delete m_impl;
	m_impl = 0;
}


void Renderer::renderScene()
{
	if(m_impl->m_update_bb && h3dQueryUnloadedResource(0) == 0)
	{
		// update boundingboxes because horde3d does not update them when geometry is loaded
		for(int i = 0; i < m_impl->m_renderables.size(); ++i)
		{
			if(m_impl->m_renderables[i].m_node == 0)
			{
				H3DRes res = h3dFindResource(H3DResTypes::SceneGraph, m_impl->m_paths[i].c_str());
				H3DNode& node = m_impl->m_renderables[i].m_node;
				node = h3dAddNodes(H3DRootNode, res);
				Matrix mtx;
				Entity(m_impl->m_universe, m_impl->m_renderables[i].m_entity).getMatrix(mtx);
				h3dSetNodeTransMat(node, &mtx.m11);
				Entity e(m_impl->m_universe, m_impl->m_renderables[i].m_entity);
			}
		}
		m_impl->m_update_bb = false;
	}
	h3dRender(m_impl->m_camera_node);
	
	h3dFinalizeFrame();
	glUseProgramEXT(0);
	glDisable(GL_DEPTH_TEST);
	for(int i = 0; i < 16; ++i)
	{
		glActiveTextureEXT(GL_TEXTURE0 + i);
		glDisable(GL_TEXTURE_2D);
	}
	glActiveTextureEXT(GL_TEXTURE0);
}


void Renderer::endFrame()
{
}


void Renderer::getLightFov(Component cmp, float& fov)
{
	fov = h3dGetNodeParamF(m_impl->m_lights[cmp.index].m_node, H3DLight::FovF, 0);
}


void Renderer::setLightFov(Component cmp, const float& fov)
{
	h3dSetNodeParamF(m_impl->m_lights[cmp.index].m_node, H3DLight::FovF, 0, fov);
}


void Renderer::getLightRadius(Component cmp, float& r)
{
	r = h3dGetNodeParamF(m_impl->m_lights[cmp.index].m_node, H3DLight::RadiusF, 0);
}


void Renderer::setLightRadius(Component cmp, const float& r)
{
	h3dSetNodeParamF(m_impl->m_lights[cmp.index].m_node, H3DLight::RadiusF, 0, r);
}


void Renderer::setVisible(Component cmp, const bool& visible)
{
	H3DNode node = m_impl->m_renderables[cmp.index].m_node;
	int flags = h3dGetNodeFlags(node);
	if(visible)
	{
		h3dSetNodeFlags(node, flags & ~H3DNodeFlags::NoDraw, true);
	}
	else
	{
		h3dSetNodeFlags(node, flags | H3DNodeFlags::NoDraw, true);
	}
}


void Renderer::getVisible(Component cmp, bool& visible)
{
	visible = !(h3dGetNodeFlags(m_impl->m_renderables[cmp.index].m_node) & H3DNodeFlags::NoDraw);
}


void Renderer::getCastShadows(Component cmp, bool& cast_shadows)
{
	cast_shadows = !(h3dGetNodeFlags(m_impl->m_renderables[cmp.index].m_node) & H3DNodeFlags::NoCastShadow);
}


void Renderer::setCastShadows(Component cmp, const bool& cast_shadows)
{
	H3DNode node = m_impl->m_renderables[cmp.index].m_node;
	int flags = h3dGetNodeFlags(node);
	if(cast_shadows)
	{
		h3dSetNodeFlags(node, flags & ~H3DNodeFlags::NoCastShadow, true);
	}
	else
	{
		h3dSetNodeFlags(node, flags | H3DNodeFlags::NoCastShadow, true);
	}
}


void Renderer::getMesh(Component cmp, string& str)
{
	str = m_impl->m_paths[cmp.index];
}


void Renderer::setMesh(Component cmp, const string& str)
{
	if(str.substr(str.length()-10, 10) == ".scene.xml")
	{
		m_impl->m_paths[cmp.index] = str;
		if(m_impl->m_renderables[cmp.index].m_node != 0)
		{
			h3dRemoveNode(m_impl->m_renderables[cmp.index].m_node);
			m_impl->m_renderables[cmp.index].m_node = 0;
		}
		H3DRes res = h3dAddResource(H3DResTypes::SceneGraph, str.c_str(), 0);
		if(h3dIsResLoaded(res))
		{
			H3DNode& node = m_impl->m_renderables[cmp.index].m_node;
			node = h3dAddNodes(H3DRootNode, res);
			Matrix mtx;
			Entity(m_impl->m_universe, m_impl->m_renderables[cmp.index].m_entity).getMatrix(mtx);
			h3dSetNodeTransMat(node, &mtx.m11);
			Entity e(m_impl->m_universe, m_impl->m_renderables[cmp.index].m_entity);
		}
		m_impl->loadResources();
	}
}


Component Renderer::getRenderable(Universe& universe, H3DNode node)
{
	for(int i = 0; i < m_impl->m_renderables.size(); ++i)
	{
		if(m_impl->m_renderables[i].m_node == node)
		{
			Entity e(m_impl->m_universe, i);
			return Component(e, rend_type, this, i);
		}
	}
	return Component::INVALID;
}


void Renderer::getRay(int x, int y, Vec3& origin, Vec3& dir)
{
	h3dutPickRay(m_impl->m_camera_node, x / (float)m_impl->m_width, (m_impl->m_height - y) / (float)m_impl->m_height, &origin.x, &origin.y, &origin.z, &dir.x, &dir.y, &dir.z);
}


void RendererImpl::postDeserialize()
{
	for(int i = 0; i < m_paths.size(); ++i)
	{
		Entity e(m_universe, m_renderables[i].m_entity);
		if(m_paths[i] != "")
		{
			H3DRes res = h3dAddResource(H3DResTypes::SceneGraph, m_paths[i].c_str(), 0);
			if(h3dIsResLoaded(res))
			{
				H3DNode& node = m_renderables[i].m_node;
				node = h3dAddNodes(H3DRootNode, res);
				Matrix mtx;
				e.getMatrix(mtx);
				h3dSetNodeTransMat(node, &mtx.m11);
			}
		}
		if(e.isValid())
		{
			m_universe->getEventManager()->emitEvent(ComponentEvent(Component(e, rend_type, m_owner, i)));			
		}
	}
	loadResources();
	for(int i = 0; i < m_lights.size(); ++i)
	{
		H3DNode& node = m_lights[i].m_node;
		node = h3dAddLightNode(H3DRootNode, "", 0, "LIGHTING", "SHADOWMAP");
		Matrix mtx;
		m_universe->getRotation(m_lights[i].m_entity).toMatrix(mtx);
		mtx.setTranslation(m_universe->getPosition(m_lights[i].m_entity));
		h3dSetNodeTransMat(node, &mtx.m11);
		h3dSetNodeParamF(node, H3DLight::RadiusF, 0, 20.0f);
		h3dSetNodeParamF(node, H3DLight::FovF, 0, 90);
		h3dSetNodeParamI(node, H3DLight::ShadowMapCountI, 1);
		h3dSetNodeParamF(node, H3DLight::ShadowMapBiasF, 0, 0.01f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 0, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 1, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 2, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorMultiplierF, 0, 1.0f);
		Entity e(m_universe, m_lights[i].m_entity);
		m_universe->getEventManager()->emitEvent(ComponentEvent(Component(e, point_light_type, m_owner, i)));
	}
}


void Renderer::destroyPointLight(Component cmp)
{
	m_impl->destroyPointLight(cmp);
}


void RendererImpl::destroyPointLight(Component cmp)
{
	ASSERT(cmp.type == point_light_type);
	h3dRemoveNode(m_lights[cmp.index].m_node);
	m_lights[cmp.index].m_entity = m_first_free_light; // entity is used for freelist
	m_first_free_light = cmp.index;
	m_universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
}


void Renderer::destroyRenderable(Component cmp)
{
	m_impl->destroyRenderable(cmp);
}


void RendererImpl::destroyRenderable(Component cmp)
{
	ASSERT(cmp.type == rend_type);
	h3dRemoveNode(m_renderables[cmp.index].m_node);
	m_paths[cmp.index] = "";
	m_renderables[cmp.index].m_entity = m_first_free_renderable; // entity is used for freelist
	m_first_free_renderable = cmp.index;
	m_universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
}


Component Renderer::createRenderable(Entity entity)
{
	int index = -1;
	if(m_impl->m_first_free_renderable == -1)
	{
		m_impl->m_renderables.push_back_empty();
		m_impl->m_renderables.back().m_entity = entity.index;
		m_impl->m_renderables.back().m_node = 0;
		m_impl->m_paths.push_back("");
		index = m_impl->m_renderables.size() - 1;
	}
	else
	{
		int next_free = m_impl->m_renderables[m_impl->m_first_free_renderable].m_entity;
		m_impl->m_renderables[m_impl->m_first_free_renderable].m_node = -1;
		m_impl->m_renderables[m_impl->m_first_free_renderable].m_entity = entity.index;
		m_impl->m_paths[m_impl->m_first_free_renderable] = "";
		index = m_impl->m_first_free_renderable;
		m_impl->m_first_free_renderable = next_free;
	}
	Component cmp(entity, rend_type, this, index);
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


void Renderer::getCameraMatrix(Matrix& mtx)
{
	const float* tmp;
	h3dGetNodeTransMats(m_impl->m_camera_node, 0, &tmp);
	for(int i = 0; i < 16; ++i)
	{
		(&mtx.m11)[i] = tmp[i];
	}
}


bool Renderer::isReady() const
{
	return m_impl->m_is_pipeline_loaded;
}


void Renderer::setCameraMatrix(const Matrix& mtx)
{
	h3dSetNodeTransMat(m_impl->m_camera_node, &mtx.m11);
}


float Renderer::getHalfFovTan()
{
	H3DNode node = m_impl->m_camera_node;
	float top = h3dGetNodeParamF(node, H3DCamera::TopPlaneF, 1);
	float n = h3dGetNodeParamF(node, H3DCamera::NearPlaneF, 1);
	return top / n;
}


Component Renderer::createPointLight(Entity entity)
{
	int index = -1;
	if(m_impl->m_first_free_light == -1)
	{
		m_impl->m_lights.push_back_empty();
		m_impl->m_lights.back().m_entity = entity.index;
		index = m_impl->m_lights.size() - 1;
	}
	else
	{
		int next_free = m_impl->m_lights[m_impl->m_first_free_light].m_entity;
		m_impl->m_lights[m_impl->m_first_free_light].m_node = -1;
		m_impl->m_lights[m_impl->m_first_free_light].m_entity = entity.index;
		index = m_impl->m_first_free_light;
		m_impl->m_first_free_light = next_free;
	}

	H3DNode& node = m_impl->m_lights[index].m_node;
	node = h3dAddLightNode(H3DRootNode, "", 0, "LIGHTING", "SHADOWMAP");
	Matrix mtx;
	entity.getMatrix(mtx);
	h3dSetNodeTransMat(node, &mtx.m11);
	h3dSetNodeParamF(node, H3DLight::RadiusF, 0, 20.0f);
	h3dSetNodeParamF(node, H3DLight::FovF, 0, 90);
	h3dSetNodeParamI(node, H3DLight::ShadowMapCountI, 1);
	h3dSetNodeParamF(node, H3DLight::ShadowMapBiasF, 0, 0.01f);
	h3dSetNodeParamF(node, H3DLight::ColorF3, 0, 1.0f);
	h3dSetNodeParamF(node, H3DLight::ColorF3, 1, 1.0f);
	h3dSetNodeParamF(node, H3DLight::ColorF3, 2, 1.0f);
	h3dSetNodeParamF(node, H3DLight::ColorMultiplierF, 0, 1.0f);
	Component cmp(entity, point_light_type, this, index);
	m_impl->m_universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


void onEvent(void* data, Event& event)
{
	static_cast<RendererImpl*>(data)->onEvent(event);
}


void Renderer::setUniverse(Universe* universe)
{
	if(m_impl->m_universe)
	{
		for(int i = 0; i < m_impl->m_renderables.size(); ++i)
		{
			h3dRemoveNode(m_impl->m_renderables[i].m_node);
		}
		m_impl->m_renderables.clear();
		for(int i = 0; i < m_impl->m_lights.size(); ++i)
		{
			h3dRemoveNode(m_impl->m_lights[i].m_node);
		}
		m_impl->m_lights.clear();
		m_impl->m_entities.clear();
		m_impl->m_paths.clear();
		m_impl->m_universe->getEventManager()->unregisterListener(EntityMovedEvent::type, m_impl, &onEvent);
		m_impl->m_universe->getEventManager()->unregisterListener(EntityDestroyedEvent::type, m_impl, &onEvent);
	}
	m_impl->m_universe = universe;
	if(m_impl->m_universe)
	{
		m_impl->m_universe->getEventManager()->registerListener(EntityMovedEvent::type, m_impl, &onEvent);
		m_impl->m_universe->getEventManager()->registerListener(EntityDestroyedEvent::type, m_impl, &onEvent);
	}
}


void RendererImpl::onEvent(Event& event)
{
	if(event.getType() == EntityMovedEvent::type)	
	{
		Matrix mtx;
		Entity entity = static_cast<EntityMovedEvent&>(event).entity;
		const Entity::ComponentList& cmps = entity.getComponents();
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == rend_type)
			{
				cmps[i].entity.getMatrix(mtx);
				h3dSetNodeTransMat(m_renderables[cmps[i].index].m_node, &mtx.m11);
				break;
			}
			else if(cmps[i].type == point_light_type)
			{
				cmps[i].entity.getMatrix(mtx);
				h3dSetNodeTransMat(m_lights[cmps[i].index].m_node, &mtx.m11);
			}
		}
	}
	else if(event.getType() == EntityDestroyedEvent::type)
	{
		Entity entity = static_cast<EntityDestroyedEvent&>(event).entity;
		const Entity::ComponentList& cmps = entity.getComponents();
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == rend_type)
			{
				destroyRenderable(cmps[i]);
				break;
			}
			else if(cmps[i].type == point_light_type)
			{
				destroyPointLight(cmps[i]);
				break;
			}
			else
			{
				ASSERT(false);
			}
		}
	}
}


const char* Renderer::getBasePath() const
{
	return m_impl->m_base_path.c_str();
}


int Renderer::getWidth() const
{
	return m_impl->m_width;
}


int Renderer::getHeight() const
{
	return m_impl->m_height;
}


H3DNode Renderer::getMeshNode(Component cmp)
{
	return m_impl->m_renderables[cmp.index].m_node; 
}


H3DNode Renderer::getRawCameraNode()
{
	return m_impl->m_camera_node;
}


void Renderer::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_impl->m_renderables.size());
	serializer.beginArray("renderables");
	for(int i = 0; i < m_impl->m_renderables.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->m_renderables[i].m_entity);
		serializer.serializeArrayItem(m_impl->m_paths[i]);
	}
	serializer.endArray();
	serializer.serialize("count", m_impl->m_lights.size());
	serializer.beginArray("lights");
	for(int i = 0; i < m_impl->m_lights.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->m_lights[i].m_entity);
	}
	serializer.endArray();
}

void Renderer::deserialize(ISerializer& serializer)
{
	int count;
	serializer.deserialize("count", count);
	m_impl->m_renderables.resize(count);
	m_impl->m_paths.resize(count);
	serializer.deserializeArrayBegin("renderables");
	for(int i = 0; i < m_impl->m_paths.size(); ++i)
	{
		m_impl->m_renderables[i].m_node = 0;
		serializer.deserializeArrayItem(m_impl->m_renderables[i].m_entity);
		serializer.deserializeArrayItem(m_impl->m_paths[i]);
	}
	serializer.deserializeArrayEnd();
	serializer.deserialize("count", count);
	m_impl->m_lights.resize(count);
	serializer.deserializeArrayBegin("lights");
	for(int i = 0; i < m_impl->m_lights.size(); ++i)
	{
		serializer.deserializeArrayItem(m_impl->m_lights[i].m_entity);
	}
	serializer.deserializeArrayEnd();
	m_impl->postDeserialize();
}


} // !namespace Lux
