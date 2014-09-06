#include "graphics/renderer.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec4.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/texture.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");


struct RendererImpl : public Renderer
{
	RendererImpl()
	{
		m_last_bind_geometry = NULL;
		m_is_editor_wireframe = false;
	}

	virtual IScene* createScene(Universe& universe)
	{
		return RenderScene::createInstance(*this, *m_engine, universe);
	}

	virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) override
	{
		glViewport(0, 0, (GLsizei)width, (GLsizei)height);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(fov, width / height, near_plane, far_plane);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		Vec3 pos = mtx.getTranslation();
		Vec3 center = pos - mtx.getZVector();
		Vec3 up = mtx.getYVector();
		gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
	}

	virtual void setRenderDevice(IRenderDevice& device) override
	{
		m_render_device = &device;
	}

	virtual void renderGame() override
	{
		PROFILE_FUNCTION();
		if (m_render_device)
		{
			m_render_device->beginFrame();
			render(*m_render_device);
			m_render_device->endFrame();
		}
	}

	virtual void render(IRenderDevice& device) override
	{
		PROFILE_FUNCTION();
		// init
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		// render
		device.getPipeline().render();

		// cleanup
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for(int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0); 
	}


	virtual void renderGeometry(Geometry& geometry, int start, int count, Shader& shader) override
	{
		PROFILE_FUNCTION();
		if (m_last_bind_geometry != &geometry)
		{
			if (m_last_bind_geometry)
			{
				m_last_bind_geometry->getVertexDefinition().end(shader);
			}
			glBindBuffer(GL_ARRAY_BUFFER, geometry.getID());
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.getIndicesID());
			m_last_bind_geometry = &geometry;
			geometry.getVertexDefinition().begin(shader);
		}
		glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GLint)));
	}


	virtual bool create(Engine& engine) override
	{
		engine.getWorldEditor()->registerProperty("camera", LUMIX_NEW(PropertyDescriptor<RenderScene>)("slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot, IPropertyDescriptor::STRING, NULL));
		engine.getWorldEditor()->registerProperty("camera", LUMIX_NEW(PropertyDescriptor<RenderScene>)("fov", &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
		engine.getWorldEditor()->registerProperty("camera", LUMIX_NEW(PropertyDescriptor<RenderScene>)("near", &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
		engine.getWorldEditor()->registerProperty("camera", LUMIX_NEW(PropertyDescriptor<RenderScene>)("far", &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));
		engine.getWorldEditor()->registerProperty("renderable", LUMIX_NEW(PropertyDescriptor<RenderScene>)("source", &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, IPropertyDescriptor::FILE, "Mesh (*.msh)"));
		engine.getWorldEditor()->registerProperty("terrain", LUMIX_NEW(PropertyDescriptor<RenderScene>)("material", &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, IPropertyDescriptor::FILE, "Material (*.mat)"));
		engine.getWorldEditor()->registerProperty("terrain", LUMIX_NEW(PropertyDescriptor<RenderScene>)("xz_scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
		engine.getWorldEditor()->registerProperty("terrain", LUMIX_NEW(PropertyDescriptor<RenderScene>)("y_scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));
		
		auto grass = LUMIX_NEW(ArrayDescriptor<RenderScene>)("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass);
		grass->addChild(LUMIX_NEW(ArrayObjectDescriptor<RenderScene>)("mesh", &RenderScene::getGrass, &RenderScene::setGrass, IPropertyDescriptor::FILE, "Mesh (*.msh)"));
		grass->addChild(LUMIX_NEW(ArrayObjectDescriptor<RenderScene>)("ground", &RenderScene::getGrassGround, &RenderScene::setGrassGround));
		grass->addChild(LUMIX_NEW(ArrayObjectDescriptor<RenderScene>)("density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity));
		engine.getWorldEditor()->registerProperty("terrain", grass);

		m_engine = &engine;
		glewExperimental = GL_TRUE;
		GLenum err = glewInit();
		return err == GLEW_OK;
	}


	virtual void destroy() override
	{
	}


	virtual const char* getName() const override
	{
		return "renderer";
	}


	virtual void enableAlphaToCoverage(bool enable) override
	{
		if (enable)
		{
			glEnable(GL_MULTISAMPLE);
			glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		}
		else
		{
			glDisable(GL_MULTISAMPLE);
			glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		}
	}


	virtual void enableZTest(bool enable) override
	{
		if (enable)
		{
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
	}

	


	virtual Engine& getEngine() override
	{
		return *m_engine;
	}


	virtual void renderModel(const Model& model, const Matrix& transform, PipelineInstance& pipeline) override
	{
		glPushMatrix();
		glMultMatrixf(&transform.m11);
		for (int i = 0, c = model.getMeshCount(); i < c;  ++i)
		{
			const Mesh& mesh = model.getMesh(i);
			mesh.getMaterial()->apply(*this, pipeline);
			model.getGeometry()->draw(mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
		}
		glPopMatrix();
	}

	virtual void serialize(ISerializer&) override
	{
	}

	virtual void deserialize(ISerializer&) override
	{
	}

	virtual void setEditorWireframe(bool is_wireframe)
	{
		m_is_editor_wireframe = is_wireframe;
	}

	virtual bool isEditorWireframe() const
	{
		return m_is_editor_wireframe;
	}

	Engine* m_engine;
	Array<Model*> m_models;	
	IRenderDevice* m_render_device;
	bool m_is_editor_wireframe;
	Geometry* m_last_bind_geometry;
};


Renderer* Renderer::createInstance()
{
	return LUMIX_NEW(RendererImpl);
}


void Renderer::destroyInstance(Renderer& renderer)
{
	LUMIX_DELETE(&renderer);
}


} // ~namespace Lumix
