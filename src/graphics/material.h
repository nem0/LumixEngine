#pragma once


#include "core/array.h"
#include "core/resource.h"


namespace Lumix
{
namespace FS
{
	class FileSystem;
	class IFile;
}

class ISerializer;
class PipelineInstance;
class Renderer;
class ResourceManager;
class Shader;
class Texture;


class LUMIX_ENGINE_API Material : public Resource
{
	friend class MaterialManager;
public:
	enum class DepthFunc
	{
		LEQUAL,
		LESS
	};
	
	struct Uniform
	{
		Uniform() : m_is_editable(false) {}

		enum Type
		{
			INT,
			FLOAT,
			MATRIX,
			TIME
		};

		static const int MAX_NAME_LENGTH = 32;
		
		char m_name[MAX_NAME_LENGTH + 1];
		uint32_t m_name_hash;
		Type m_type;
		union
		{
			int32_t m_int;
			float m_float;
			float m_matrix[16];
		};
		bool m_is_editable;
	};

public:
	void apply(Renderer& renderer, PipelineInstance& pipeline) const;
	bool isZTest() const { return m_is_z_test; }
	void enableZTest(bool enable) { m_is_z_test = enable; }
	bool isBackfaceCulling() const { return m_is_backface_culling; }
	void enableBackfaceCulling(bool enable) { m_is_backface_culling = enable; }
	bool isAlphaToCoverage() const { return m_is_alpha_to_coverage; }
	void enableAlphaToCoverage(bool enable) { m_is_alpha_to_coverage = enable; }
	bool isAlphaCutout() const { return m_is_alpha_cutout; }
	void enableAlphaCutout(bool enable) { m_is_alpha_cutout = enable; updateShaderCombination(); }
	bool isShadowReceiver() const { return m_is_shadow_receiver; }
	void enableShadowReceiving(bool enable) { m_is_shadow_receiver = enable; updateShaderCombination(); }

	void setShader(Shader* shader);
	Shader* getShader() const { return m_shader; }

	int getTextureCount() const { return m_textures.size(); }
	Texture* getTexture(int i) const { return m_textures[i].m_texture; }
	Texture* getTextureByUniform(const char* uniform) const;
	void addTexture(Texture* texture);
	void setTexture(int i, Texture* texture);
	void removeTexture(int i);
	bool save(ISerializer& serializer);
	int getUniformCount() const { return m_uniforms.size(); }
	Uniform& getUniform(int index) { return m_uniforms[index]; }

private:
	Material(const Path& path, ResourceManager& resource_manager)
		: Resource(path, resource_manager)
		, m_shader(NULL)
		, m_is_z_test(true)
		, m_is_backface_culling(true)
		, m_depth_func(DepthFunc::LESS)
		, m_is_alpha_to_coverage(false)
		, m_is_alpha_cutout(false)
		, m_shader_combination(0)
		, m_is_shadow_receiver(true)
	{ 
		updateShaderCombination();
	}

	~Material();

	virtual void doUnload(void) override;
	virtual FS::ReadCallback getReadCallback() override;

	void loaded(FS::IFile* file, bool success, FS::FileSystem& fs);
	bool deserializeTexture(ISerializer& serializer, const char* material_dir);

private:
	struct TextureInfo
	{
		TextureInfo()
		{
			m_texture = NULL;
			m_keep_data = false;
			m_uniform[0] = '\0';
		}

		Texture* m_texture;
		bool m_keep_data;
		char m_uniform[Uniform::MAX_NAME_LENGTH];
	};

private:
	void deserializeUniforms(ISerializer& serializer);
	void updateShaderCombination();
	void shaderLoaded(Resource::State old_state, Resource::State new_state);

private:
	Shader*	m_shader;
	Array<TextureInfo> m_textures;
	Array<Uniform> m_uniforms;
	bool m_is_z_test;
	bool m_is_backface_culling;
	bool m_is_alpha_to_coverage;
	bool m_is_alpha_cutout;
	bool m_is_shadow_receiver;
	DepthFunc m_depth_func;
	uint32_t m_shader_combination;
};

} // ~namespace Lumix
