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
		m_last_program_id = 0xffffFFFF;
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
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		device.getPipeline().render();

		cleanup();
	}

	virtual void cleanup() override
	{
		m_last_bind_geometry = NULL;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for(int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0); 
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, int value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform1i(loc, value);
		}
	}
	
	
	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Vec3& value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform3f(loc, value.x, value.y, value.z);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, float value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform1f(loc, value);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix& mtx) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			//glProgramUniformMatrix4fv(shader.getProgramId(), loc, 1, false, &mtx.m11);
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniformMatrix4fv(loc, 1, false, &mtx.m11);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix* matrices, int count) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniformMatrix4fv(loc, count, false, (float*)matrices);
		}
	}


	virtual void applyShader(const Shader& shader) override
	{
		GLuint id = shader.getProgramId();
		m_last_program_id = id;
		glUseProgram(id);
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


	void registerPropertyDescriptors(WorldEditor& editor)
	{
		editor.registerProperty("camera", LUMIX_NEW(StringPropertyDescriptor<RenderScene>)("slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot));
		editor.registerProperty("camera", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("fov", &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
		editor.registerProperty("camera", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("near", &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
		editor.registerProperty("camera", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("far", &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));

		editor.registerProperty("renderable", LUMIX_NEW(FilePropertyDescriptor<RenderScene>)("source", &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, "Mesh (*.msh)"));

		editor.registerProperty("terrain", LUMIX_NEW(FilePropertyDescriptor<RenderScene>)("material", &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, "Material (*.mat)"));
		editor.registerProperty("terrain", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("xz_scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
		editor.registerProperty("terrain", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("y_scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));

		auto grass = LUMIX_NEW(ArrayDescriptor<RenderScene>)("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass);
		grass->addChild(LUMIX_NEW(FileArrayObjectDescriptor<RenderScene>)("mesh", &RenderScene::getGrass, &RenderScene::setGrass, "Mesh (*.msh)"));
		auto ground = LUMIX_NEW(IntArrayObjectDescriptor<RenderScene>)("ground", &RenderScene::getGrassGround, &RenderScene::setGrassGround);
		ground->setLimit(0, 4);
		grass->addChild(ground);
		grass->addChild(LUMIX_NEW(IntArrayObjectDescriptor<RenderScene>)("density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity));
		editor.registerProperty("terrain", grass);
	}


	virtual bool create(Engine& engine) override
	{
		registerPropertyDescriptors(*engine.getWorldEditor());

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
			renderGeometry(*model.getGeometry(), mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
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
	IRenderDevice* m_render_device;
	bool m_is_editor_wireframe;
	Geometry* m_last_bind_geometry;
	GLuint m_last_program_id;
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
