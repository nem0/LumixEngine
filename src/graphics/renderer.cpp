#include "renderer.h"
#include "universe/component_event.h"
#include <cassert>
#include <cmath>
#include "Horde3D.h"
#include "Horde3DUtils.h"
#include "universe/universe.h"
#include "core/vec3.h"
#include "core/matrix.h"
#include "core/quat.h"
#include "core/crc32.h"
#include "universe/entity_moved_event.h"
#include "universe/entity_destroyed_event.h"
#include <Windows.h>
#include "gl/GL.h"
#include <cstdio>
#include "core/ifilesystem.h"
#include "core/json_serializer.h"


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
	H3DNode node;
	int entity;
};


struct RendererImpl
{
	void loadResources();
	void postDeserialize();
	void onEvent(Event& event);
	void onResize(int w, int h);
	void destroyPointLight(Component cmp);
	void destroyRenderable(Component cmp);

	int					width;
	int					height;
	vector<RenderNode>	lights;
	vector<RenderNode>	renderables;
	vector<string>		paths;
	vector<Entity>		entities;
	H3DRes				pipeline_handle;
	Universe*			universe;
	H3DNode				camera_node;
	int					first_free_renderable;
	int					first_free_light;
	string				base_path;
	IFileSystem*		file_system;
	H3DRes				loading_res;
	Renderer*			owner;
	bool				update_bb;
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
	width = w;
	height = h;
	h3dResizePipelineBuffers(pipeline_handle, width, height);
	h3dSetNodeParamI(camera_node, H3DCamera::ViewportXI, 0);
	h3dSetNodeParamI(camera_node, H3DCamera::ViewportYI, 0);
	h3dSetNodeParamI(camera_node, H3DCamera::ViewportWidthI, width);
	h3dSetNodeParamI(camera_node, H3DCamera::ViewportHeightI, height);
	h3dSetupCameraView(camera_node, 45.0f, (float)width / height, 0.1f, 1000.0f);
}


void resourceLoaded(void* user_data, char* file_data, int length, bool success)
{
	assert(success);
	RendererImpl* renderer = static_cast<RendererImpl*>(user_data);
	if(success)
	{
		renderer->update_bb = true;
		h3dLoadResource(renderer->loading_res, file_data, length);
		if(renderer->loading_res == renderer->pipeline_handle)
		{
			h3dResizePipelineBuffers(renderer->pipeline_handle, renderer->width, renderer->height);
	//		h3dSetOption( H3DOptions::DebugViewMode, 1 );
			h3dSetOption( H3DOptions::LoadTextures, 1 );
			h3dSetOption( H3DOptions::TexCompression, 0 );
			h3dSetOption( H3DOptions::FastAnimation, 0 );
			h3dSetOption( H3DOptions::MaxAnisotropy, 4 );
			h3dSetOption( H3DOptions::ShadowMapSize, 512 );
			renderer->camera_node = h3dAddCameraNode(H3DRootNode, "", renderer->pipeline_handle);
			renderer->onResize(renderer->width, renderer->height);
		}
		renderer->loading_res = 0;
		renderer->loadResources();
	}
	else
	{
		h3dLoadResource(renderer->loading_res, 0, 0);
	}
}


void RendererImpl::loadResources()
{
	H3DRes res = h3dQueryUnloadedResource(0);
	char path[255]; /// TODO 255 -> platform constant
	if(res != 0 && res != loading_res)
	{
		loading_res = res;
		sprintf_s(path, "%s%s/%s", base_path.c_str(), h3dutGetResourcePath(h3dGetResType(res)), h3dGetResName(res));
		file_system->openFile(path, &resourceLoaded, this);
	}
}


bool Renderer::create(IFileSystem* fs, int w, int h, const char* base_path)
{
	m_impl = new RendererImpl();
	m_impl->owner = this;
	m_impl->file_system = fs;
	m_impl->first_free_renderable = -1;
	m_impl->first_free_light = -1;
	m_impl->width = -1;
	m_impl->height = -1;
	m_impl->universe = 0;

	m_impl->base_path = base_path;
	m_impl->width = w;
	m_impl->height = h;
	
	if(!h3dInit())
	{	
		delete m_impl;
		m_impl = 0;
		h3dutDumpMessages();
		return false;
	}
	m_impl->pipeline_handle = h3dAddResource(H3DResTypes::Pipeline, "pipelines/forward.pipeline.xml", 0);
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
	if(m_impl->update_bb && h3dQueryUnloadedResource(0) == 0)
	{
		// update boundingboxes because horde3d does not update them when geometry is loaded
		for(int i = 0; i < m_impl->renderables.size(); ++i)
		{
			if(m_impl->renderables[i].node == 0)
			{
				H3DRes res = h3dFindResource(H3DResTypes::SceneGraph, m_impl->paths[i].c_str());
				H3DNode& node = m_impl->renderables[i].node;
				node = h3dAddNodes(H3DRootNode, res);
				Matrix mtx;
				Entity(m_impl->universe, m_impl->renderables[i].entity).getMatrix(mtx);
				h3dSetNodeTransMat(node, &mtx.m11);
				Entity e(m_impl->universe, m_impl->renderables[i].entity);
				m_impl->universe->getEventManager()->emitEvent(ComponentEvent(Component(e, rend_type, this, i)));			
			}
		}
		m_impl->update_bb = false;
	}
	h3dRender(m_impl->camera_node);
	
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
	fov = h3dGetNodeParamF(m_impl->lights[cmp.index].node, H3DLight::FovF, 0);
}


void Renderer::setLightFov(Component cmp, const float& fov)
{
	h3dSetNodeParamF(m_impl->lights[cmp.index].node, H3DLight::FovF, 0, fov);
}


void Renderer::getLightRadius(Component cmp, float& r)
{
	r = h3dGetNodeParamF(m_impl->lights[cmp.index].node, H3DLight::RadiusF, 0);
}


void Renderer::setLightRadius(Component cmp, const float& r)
{
	h3dSetNodeParamF(m_impl->lights[cmp.index].node, H3DLight::RadiusF, 0, r);
}


void Renderer::getMesh(Component cmp, string& str)
{
	str = m_impl->paths[cmp.index];
}


void Renderer::setMesh(Component cmp, const string& str)
{
	m_impl->paths[cmp.index] = str;
	H3DRes res = h3dAddResource(H3DResTypes::SceneGraph, str.c_str(), 0);
	if(h3dIsResLoaded(res))
	{
		H3DNode& node = m_impl->renderables[cmp.index].node;
		node = h3dAddNodes(H3DRootNode, res);
		Matrix mtx;
		Entity(m_impl->universe, m_impl->renderables[cmp.index].entity).getMatrix(mtx);
		h3dSetNodeTransMat(node, &mtx.m11);
		Entity e(m_impl->universe, m_impl->renderables[cmp.index].entity);
	}
	m_impl->loadResources();
}


Component Renderer::getRenderable(Universe& universe, H3DNode node)
{
	for(int i = 0, c = universe.getComponents().size(); i < c; ++i)
	{
		const vector<Component>& cmps = universe.getComponents()[i];
		for(int j = 0, cj = cmps.size(); j < cj; ++j)
		{		
			if(cmps[j].type == rend_type && m_impl->renderables[cmps[j].index].node == node)
			{
				return cmps[j];
			}
		}
	}
	return Component::INVALID;
}


void Renderer::getRay(int x, int y, Vec3& origin, Vec3& dir)
{
	h3dutPickRay(m_impl->camera_node, x / (float)m_impl->width, (m_impl->height - y) / (float)m_impl->height, &origin.x, &origin.y, &origin.z, &dir.x, &dir.y, &dir.z);
}


void RendererImpl::postDeserialize()
{
	for(int i = 0; i < paths.size(); ++i)
	{
		if(paths[i] != "")
		{
			H3DRes res = h3dAddResource(H3DResTypes::SceneGraph, paths[i].c_str(), 0);
			if(h3dIsResLoaded(res))
			{
				H3DNode& node = renderables[i].node;
				node = h3dAddNodes(H3DRootNode, res);
				Matrix mtx;
				Entity(universe, renderables[i].entity).getMatrix(mtx);
				h3dSetNodeTransMat(node, &mtx.m11);
				Entity e(universe, renderables[i].entity);
				universe->getEventManager()->emitEvent(ComponentEvent(Component(e, rend_type, owner, i)));			
			}
		}
	}
	loadResources();
	for(int i = 0; i < lights.size(); ++i)
	{
		H3DNode& node = lights[i].node;
		node = h3dAddLightNode(H3DRootNode, "", 0, "LIGHTING", "SHADOWMAP");
		Matrix mtx;
		universe->getRotations()[lights[i].entity].toMatrix(mtx);
		mtx.setTranslation(universe->getPositions()[lights[i].entity]);
		h3dSetNodeTransMat(node, &mtx.m11);
		h3dSetNodeParamF(node, H3DLight::RadiusF, 0, 20.0f);
		h3dSetNodeParamF(node, H3DLight::FovF, 0, 90);
		h3dSetNodeParamI(node, H3DLight::ShadowMapCountI, 1);
		h3dSetNodeParamF(node, H3DLight::ShadowMapBiasF, 0, 0.01f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 0, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 1, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorF3, 2, 1.0f);
		h3dSetNodeParamF(node, H3DLight::ColorMultiplierF, 0, 1.0f);
		Entity e(universe, lights[i].entity);
		universe->getEventManager()->emitEvent(ComponentEvent(Component(e, point_light_type, owner, i)));
	}
}


void Renderer::destroyPointLight(Component cmp)
{
	m_impl->destroyPointLight(cmp);
}


void RendererImpl::destroyPointLight(Component cmp)
{
	assert(cmp.type == point_light_type);
	h3dRemoveNode(lights[cmp.index].node);
	lights[cmp.index].entity = first_free_light; // entity is used for freelist
	first_free_light = cmp.index;
	universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
}


void Renderer::destroyRenderable(Component cmp)
{
	m_impl->destroyRenderable(cmp);
}


void RendererImpl::destroyRenderable(Component cmp)
{
	assert(cmp.type == rend_type);
	h3dRemoveNode(renderables[cmp.index].node);
	paths[cmp.index] = "";
	renderables[cmp.index].entity = first_free_renderable; // entity is used for freelist
	first_free_renderable = cmp.index;
	universe->getEventManager()->emitEvent(ComponentEvent(cmp, false));
}


Component Renderer::createRenderable(Entity entity)
{
	int index = -1;
	if(m_impl->first_free_renderable == -1)
	{
		m_impl->renderables.push_back_empty();
		m_impl->renderables.back().entity = entity.index;
		m_impl->renderables.back().node = 0;
		m_impl->paths.push_back("");
		index = m_impl->renderables.size() - 1;
	}
	else
	{
		int next_free = m_impl->renderables[m_impl->first_free_renderable].entity;
		m_impl->renderables[m_impl->first_free_renderable].node = -1;
		m_impl->renderables[m_impl->first_free_renderable].entity = entity.index;
		m_impl->paths[m_impl->first_free_renderable] = "";
		index = m_impl->first_free_renderable;
		m_impl->first_free_renderable = next_free;
	}
	Component cmp(entity, rend_type, this, index);
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


void Renderer::getCameraMatrix(Matrix& mtx)
{
	const float* tmp;
	h3dGetNodeTransMats(m_impl->camera_node, 0, &tmp);
	for(int i = 0; i < 16; ++i)
	{
		(&mtx.m11)[i] = tmp[i];
	}}


void Renderer::setCameraMatrix(const Matrix& mtx)
{
	h3dSetNodeTransMat(m_impl->camera_node, &mtx.m11);
}


float Renderer::getHalfFovTan()
{
	H3DNode node = m_impl->camera_node;
	float top = h3dGetNodeParamF(node, H3DCamera::TopPlaneF, 1);
	float n = h3dGetNodeParamF(node, H3DCamera::NearPlaneF, 1);
	return top / n;
}


Component Renderer::createPointLight(Entity entity)
{
	int index = -1;
	if(m_impl->first_free_light == -1)
	{
		m_impl->lights.push_back_empty();
		m_impl->lights.back().entity = entity.index;
		index = m_impl->lights.size() - 1;
	}
	else
	{
		int next_free = m_impl->lights[m_impl->first_free_light].entity;
		m_impl->lights[m_impl->first_free_light].node = -1;
		m_impl->lights[m_impl->first_free_light].entity = entity.index;
		index = m_impl->first_free_light;
		m_impl->first_free_light = next_free;
	}

	H3DNode& node = m_impl->lights[index].node;
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
	m_impl->universe->getEventManager()->emitEvent(ComponentEvent(cmp));
	return cmp;
}


void onEvent(void* data, Event& event)
{
	static_cast<RendererImpl*>(data)->onEvent(event);
}


void Renderer::setUniverse(Universe* universe)
{
	if(m_impl->universe)
	{
		for(int i = 0; i < m_impl->renderables.size(); ++i)
		{
			h3dRemoveNode(m_impl->renderables[i].node);
		}
		m_impl->renderables.clear();
		for(int i = 0; i < m_impl->lights.size(); ++i)
		{
			h3dRemoveNode(m_impl->lights[i].node);
		}
		m_impl->lights.clear();
		m_impl->entities.clear();
		m_impl->paths.clear();
		m_impl->universe->getEventManager()->unregisterListener(EntityMovedEvent::type, m_impl, &onEvent);
		m_impl->universe->getEventManager()->unregisterListener(EntityDestroyedEvent::type, m_impl, &onEvent);
	}
	m_impl->universe = universe;
	if(m_impl->universe)
	{
		m_impl->universe->getEventManager()->registerListener(EntityMovedEvent::type, m_impl, &onEvent);
		m_impl->universe->getEventManager()->registerListener(EntityDestroyedEvent::type, m_impl, &onEvent);
	}
}


void RendererImpl::onEvent(Event& event)
{
	if(event.getType() == EntityMovedEvent::type)	
	{
		Matrix mtx;
		Entity entity = static_cast<EntityMovedEvent&>(event).entity;
		const vector<Component>& cmps = entity.universe->getComponents(entity);
		for(int i = 0, c = cmps.size(); i < c; ++i)
		{
			if(cmps[i].type == rend_type)
			{
				cmps[i].entity.getMatrix(mtx);
				h3dSetNodeTransMat(renderables[cmps[i].index].node, &mtx.m11);
				break;
			}
			else if(cmps[i].type == point_light_type)
			{
				cmps[i].entity.getMatrix(mtx);
				h3dSetNodeTransMat(lights[cmps[i].index].node, &mtx.m11);
			}
		}
	}
	else if(event.getType() == EntityDestroyedEvent::type)
	{
		Entity entity = static_cast<EntityDestroyedEvent&>(event).entity;
		const vector<Component>& cmps = entity.universe->getComponents(entity);
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
				assert(false);
			}
		}
	}
}


const char* Renderer::getBasePath() const
{
	return m_impl->base_path.c_str();
}


int Renderer::getWidth() const
{
	return m_impl->width;
}


int Renderer::getHeight() const
{
	return m_impl->height;
}


H3DNode Renderer::getMeshNode(Component cmp)
{
	return m_impl->renderables[cmp.index].node; 
}


H3DNode Renderer::getRawCameraNode()
{
	return m_impl->camera_node;
}


void Renderer::serialize(ISerializer& serializer)
{
	serializer.serialize("count", m_impl->renderables.size());
	serializer.beginArray("renderables");
	for(int i = 0; i < m_impl->renderables.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->renderables[i].entity);
		serializer.serializeArrayItem(m_impl->paths[i]);
	}
	serializer.endArray();
	serializer.serialize("count", m_impl->lights.size());
	serializer.beginArray("lights");
	for(int i = 0; i < m_impl->lights.size(); ++i)
	{
		serializer.serializeArrayItem(m_impl->lights[i].entity);
	}
	serializer.endArray();
}

void Renderer::deserialize(ISerializer& serializer)
{
	int count;
	serializer.deserialize("count", count);
	m_impl->renderables.resize(count);
	m_impl->paths.resize(count);
	serializer.deserializeArrayBegin("renderables");
	for(int i = 0; i < m_impl->paths.size(); ++i)
	{
		m_impl->renderables[i].node = 0;
		serializer.deserializeArrayItem(m_impl->renderables[i].entity);
		serializer.deserializeArrayItem(m_impl->paths[i]);
	}
	serializer.deserializeArrayEnd();
	serializer.deserialize("count", count);
	m_impl->lights.resize(count);
	serializer.deserializeArrayBegin("lights");
	for(int i = 0; i < m_impl->lights.size(); ++i)
	{
		serializer.deserializeArrayItem(m_impl->lights[i].entity);
	}
	serializer.deserializeArrayEnd();
	m_impl->postDeserialize();
}


} // !namespace Lux
