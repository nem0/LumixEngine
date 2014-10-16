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
		m_current_pass_hash = crc32("MAIN");
		m_last_bind_geometry = NULL;
		m_last_program_id = 0xffffFFFF;
		m_is_editor_wireframe = false;
	}

	virtual IScene* createScene(Universe& universe)
	{
		return RenderScene::createInstance(*this, *m_engine, universe);
	}

	virtual void setViewMatrix(const Matrix& matrix) override
	{
		m_view_matrix = matrix;
	}

	virtual void setProjectionMatrix(const Matrix& matrix) override
	{
		m_projection_matrix = matrix;
	}

	virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) override
	{
		glViewport(0, 0, (GLsizei)width, (GLsizei)height);
		getProjectionMatrix(fov, width, height, near_plane, far_plane, &m_projection_matrix);

		Vec3 pos = mtx.getTranslation();
		Vec3 center = pos - mtx.getZVector();
		Vec3 up = mtx.getYVector();
		getLookAtMatrix(pos, center, up, &m_view_matrix);
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

	virtual const Matrix& getCurrentViewMatrix() override
	{
		return m_view_matrix;
	}

	virtual const Matrix& getCurrentProjectionMatrix() override
	{
		return m_projection_matrix;
	}

	virtual void cleanup() override
	{
		if(m_last_bind_geometry)
		{
			m_last_bind_geometry->getVertexDefinition().end(*m_last_bind_geometry_shader);
		}
		m_last_bind_geometry = NULL;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for(int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glBindTexture(GL_TEXTURE_2D, 0);
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


	virtual void setFixedCachedUniform(const Shader& shader, int name, const Vec3& value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
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

	virtual void setFixedCachedUniform(const Shader& shader, int name, const Vec4& value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform4f(loc, value.x, value.y, value.z, value.w);
		}
	}

	virtual void setFixedCachedUniform(const Shader& shader, int name, float value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
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



	virtual void setFixedCachedUniform(const Shader& shader, int name, const Matrix& mtx) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
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


	virtual void setFixedCachedUniform(const Shader& shader, int name, const Matrix* matrices, int count) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
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


	virtual uint32_t getPass() override
	{
		return m_current_pass_hash;
	}


	virtual void setPass(uint32_t pass_hash) override
	{
		m_current_pass_hash = pass_hash;
	}


	virtual Shader& getDebugShader() override
	{
		ASSERT(m_debug_shader);
		return *m_debug_shader;
	}


	virtual void applyShader(Shader& shader, uint32_t combination) override
	{
		shader.setCurrentCombination(combination, m_current_pass_hash);
		GLuint id = shader.getProgramId();
		m_last_program_id = id;
		glUseProgram(id);
		RendererImpl::setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::VIEW_MATRIX, m_view_matrix);
		RendererImpl::setFixedCachedUniform(shader, (int)Shader::FixedCachedUniforms::PROJECTION_MATRIX, m_projection_matrix);
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
			m_last_bind_geometry_shader = &shader;
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

		editor.registerProperty("renderable", LUMIX_NEW(ResourcePropertyDescriptor<RenderScene>)("source", &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, "Mesh (*.msh)"));

		editor.registerProperty("light", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("ambient_intensity", &RenderScene::getLightAmbientIntensity, &RenderScene::setLightAmbientIntensity));
		editor.registerProperty("light", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("diffuse_intensity", &RenderScene::getLightDiffuseIntensity, &RenderScene::setLightDiffuseIntensity));
		editor.registerProperty("light", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("fog_density", &RenderScene::getFogDensity, &RenderScene::setFogDensity));
		editor.registerProperty("light", LUMIX_NEW(ColorPropertyDescriptor<RenderScene>)("ambient_color", &RenderScene::getLightAmbientColor, &RenderScene::setLightAmbientColor));
		editor.registerProperty("light", LUMIX_NEW(ColorPropertyDescriptor<RenderScene>)("diffuse_color", &RenderScene::getLightDiffuseColor, &RenderScene::setLightDiffuseColor));
		editor.registerProperty("light", LUMIX_NEW(ColorPropertyDescriptor<RenderScene>)("fog_color", &RenderScene::getFogColor, &RenderScene::setFogColor));

		editor.registerProperty("terrain", LUMIX_NEW(ResourcePropertyDescriptor<RenderScene>)("material", &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, "Material (*.mat)"));
		editor.registerProperty("terrain", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("xz_scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
		editor.registerProperty("terrain", LUMIX_NEW(DecimalPropertyDescriptor<RenderScene>)("y_scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));

		auto grass = LUMIX_NEW(ArrayDescriptor<RenderScene>)("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass);
		grass->addChild(LUMIX_NEW(ResourceArrayObjectDescriptor<RenderScene>)("mesh", &RenderScene::getGrass, &RenderScene::setGrass, "Mesh (*.msh)"));
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
		m_debug_shader = static_cast<Shader*>(engine.getResourceManager().get(ResourceManager::SHADER)->load("shaders/debug.shd"));
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
		for (int i = 0, c = model.getMeshCount(); i < c;  ++i)
		{
			const Mesh& mesh = model.getMesh(i);
			mesh.getMaterial()->apply(*this, pipeline);
			RendererImpl::setFixedCachedUniform(*mesh.getMaterial()->getShader(), (int)Shader::FixedCachedUniforms::WORLD_MATRIX, transform);
			RendererImpl::renderGeometry(*model.getGeometry(), mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
		}
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
	Shader* m_last_bind_geometry_shader;
	GLuint m_last_program_id;
	uint32_t m_current_pass_hash;
	Matrix m_view_matrix;
	Matrix m_projection_matrix;
	Shader* m_debug_shader;
};


Renderer* Renderer::createInstance()
{
	return LUMIX_NEW(RendererImpl);
}


void Renderer::destroyInstance(Renderer& renderer)
{
	LUMIX_DELETE(&renderer);
}


void Renderer::getProjectionMatrix(float fov, float width, float height, float near_plane, float far_plane, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	float f = 1 / tanf(Math::degreesToRadians(fov) * 0.5f);
	mtx->m11 = f / (width / height);
	mtx->m22 = f;
	mtx->m33 = (far_plane + near_plane) / (near_plane - far_plane);
	mtx->m44 = 0;
	mtx->m43 = (2 * far_plane * near_plane) / (near_plane - far_plane);
	mtx->m34 = -1;
}


void Renderer::getOrthoMatrix(float left, float right, float bottom, float top, float z_near, float z_far, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	mtx->m11 = 2 / (right - left);
	mtx->m22 = 2 / (top - bottom);
	mtx->m33 = -2 / (z_far - z_near);
	mtx->m41 = -(right + left) / (right - left);
	mtx->m42 = -(top + bottom) / (top - bottom);
	mtx->m43 = -(z_far + z_near) / (z_far - z_near);
	/*		glOrtho(left, right, bottom, top, z_near, z_far);
	glGetFloatv(GL_PROJECTION_MATRIX, &mtx->m11);
	*/
}


void Renderer::getLookAtMatrix(const Vec3& pos, const Vec3& center, const Vec3& up, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	Vec3 f = center - pos;
	f.normalize();
	Vec3 r = crossProduct(f, up);
	r.normalize();
	Vec3 u = crossProduct(r, f);
	mtx->setXVector(r);
	mtx->setYVector(u);
	mtx->setZVector(-f);
	mtx->transpose();
	mtx->setTranslation(Vec3(-dotProduct(r, pos), -dotProduct(u, pos), dotProduct(f, pos)));
	/*glPushMatrix();
	float m[16];
	gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
	glGetFloatv(GL_MODELVIEW_MATRIX, m);
	glPopMatrix();*/
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec3& value)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::setFixedCachedUniform(shader, name, value);
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec4& value)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::setFixedCachedUniform(shader, name, value);
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, float value)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::setFixedCachedUniform(shader, name, value);
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix& mtx)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::setFixedCachedUniform(shader, name, mtx);
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix* matrices, int count)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::setFixedCachedUniform(shader, name, matrices, count);
}


void renderGeometry(Renderer& renderer, Geometry& geometry, int start, int count, Shader& shader)
{
	static_cast<RendererImpl&>(renderer).RendererImpl::renderGeometry(geometry, start, count, shader);
}


} // ~namespace Lumix
