#pragma once

#include "engine/allocator.h"
#include "engine/lua_wrapper.h"
#include "engine/stream.h"
#include "engine/string.h"
#include "fbx_importer.h"


namespace Lumix {

struct ModelMeta {
	static const char* toString(FBXImporter::ImportConfig::Physics value) {
		switch (value) {
			case FBXImporter::ImportConfig::Physics::TRIMESH: return "Triangle mesh";
			case FBXImporter::ImportConfig::Physics::CONVEX: return "Convex";
			case FBXImporter::ImportConfig::Physics::NONE: return "None";
		}
		ASSERT(false);
		return "none";
	}

	ModelMeta(IAllocator& allocator) : clips(allocator), root_motion_bone(allocator) {}

	void serialize(OutputMemoryStream& blob, const Path& path) {
		if(physics != FBXImporter::ImportConfig::Physics::NONE) blob << "\nphysics = \"" << toString(physics) << "\"";
		blob << "\nlod_count = " << lod_count;

		#define WRITE_BOOL(id, default_value) \
			if ((id) != (default_value)) { \
				blob << "\n" << #id << (id ? " = true" : " = false"); \
			}

		#define WRITE_VALUE(id, default_value) \
			if ((id) != (default_value)) { blob << "\n" << #id << " = " << id; }
		
		WRITE_BOOL(create_impostor, false);
		WRITE_BOOL(use_mikktspace, false);
		WRITE_BOOL(force_skin, false);
		WRITE_BOOL(bake_vertex_ao, false);
		WRITE_BOOL(bake_impostor_normals, false);
		WRITE_BOOL(split, false);
		WRITE_BOOL(import_vertex_colors, false);
		WRITE_BOOL(vertex_color_is_ao, false);
		WRITE_BOOL(ignore_animations, false);
		WRITE_VALUE(anim_translation_error, 1.f);
		WRITE_VALUE(anim_rotation_error, 1.f);
		WRITE_VALUE(scale, 1.f);
		WRITE_VALUE(culling_scale, 1.f);
		WRITE_VALUE(root_motion_flags, Animation::Flags::NONE);

		#undef WRITE_BOOL
		#undef WRITE_VALUE

		if (root_motion_bone.length() > 0) blob << "\nroot_motion_bone = \"" << root_motion_bone << "\"";
		if (!skeleton.isEmpty()) {
			StringView dir = Path::getDir(Path::getResource(path));
			if (!dir.empty() && startsWith(skeleton, dir)) {
				blob << "\nskeleton_rel = \"" << skeleton.c_str() + dir.size();
			}
			else {
				blob << "\nskeleton = \"" << skeleton;
			}
			blob << "\"";
		}

		if (!clips.empty()) {
			blob << "\nclips = {";
			for (const FBXImporter::ImportConfig::Clip& clip : clips) {
				blob << "\n\n{";
				blob << "\n\n\nname = \"" << clip.name << "\",";
				blob << "\n\n\nfrom_frame = " << clip.from_frame << ",";
				blob << "\n\n\nto_frame = " << clip.to_frame;
				blob << "\n\n},";
			}
			blob << "\n}";
		}

		if (autolod_mask & 1) blob << "\nautolod0 = " << autolod_coefs[0];
		if (autolod_mask & 2) blob << "\nautolod1 = " << autolod_coefs[1];
		if (autolod_mask & 4) blob << "\nautolod2 = " << autolod_coefs[2];
		if (autolod_mask & 4) blob << "\nautolod3 = " << autolod_coefs[3];

		for (u32 i = 0; i < lengthOf(lods_distances); ++i) {
			if (lods_distances[i] > 0) {
				blob << "\nlod" << i << "_distance" << " = " << lods_distances[i];
			}
		}
	}

	bool deserialize(InputMemoryStream& blob, const Path& path) {
		ASSERT(blob.getPosition() == 0);
		lua_State* L = luaL_newstate();
		if (!LuaWrapper::execute(L, StringView((const char*)blob.getData(), (u32)blob.size()), path.c_str(), 0)) {
			return false;
		}
		
		deserialize(L, path);

		lua_close(L);
		return true;	
	}

	void deserialize(lua_State* L, const Path& path) {
		LuaWrapper::DebugGuard guard(L);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "root_motion_flags", (i32*)&root_motion_flags);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "use_mikktspace", &use_mikktspace);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "force_skin", &force_skin);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "anim_rotation_error", &anim_rotation_error);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "anim_translation_error", &anim_translation_error);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "scale", &scale);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "culling_scale", &culling_scale);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "split", &split);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "bake_impostor_normals", &bake_impostor_normals);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "bake_vertex_ao", &bake_vertex_ao);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "create_impostor", &create_impostor);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "import_vertex_colors", &import_vertex_colors);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "ignore_animations", &ignore_animations);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "vertex_color_is_ao", &vertex_color_is_ao);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "lod_count", &lod_count);
			
		if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod0", &autolod_coefs[0])) autolod_mask |= 1;
		if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod1", &autolod_coefs[1])) autolod_mask |= 2;
		if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod2", &autolod_coefs[2])) autolod_mask |= 4;
		if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod3", &autolod_coefs[3])) autolod_mask |= 8;

		clips.clear();
		if (LuaWrapper::getField(L, LUA_GLOBALSINDEX, "clips") == LUA_TTABLE) {
			const size_t count = lua_objlen(L, -1);
			for (int i = 0; i < count; ++i) {
				lua_rawgeti(L, -1, i + 1);
				if (lua_istable(L, -1)) {
					FBXImporter::ImportConfig::Clip& clip = clips.emplace();
					char name[128];
					if (!LuaWrapper::checkStringField(L, -1, "name", Span(name)) 
						|| !LuaWrapper::checkField(L, -1, "from_frame", &clip.from_frame)
						|| !LuaWrapper::checkField(L, -1, "to_frame", &clip.to_frame))
					{
						logError(path, ": clip ", i, " is invalid");
						clips.pop();
						continue;
					}
					clip.name = name;
				}
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);

		char tmp[MAX_PATH];
		if (LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "root_motion_bone", Span(tmp))) root_motion_bone = tmp;
		if (LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "skeleton", Span(tmp))) skeleton = tmp;
		if (LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "skeleton_rel", Span(tmp))) {
			StringView dir = Path::getDir(Path::getResource(path));
			skeleton = Path(dir, "/", tmp);
		}
		if (LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "physics", Span(tmp))) {
			if (equalIStrings(tmp, "trimesh")) physics = FBXImporter::ImportConfig::Physics::TRIMESH;
			else if (equalIStrings(tmp, "convex")) physics = FBXImporter::ImportConfig::Physics::CONVEX;
			else physics = FBXImporter::ImportConfig::Physics::NONE;
		}

		for (u32 i = 0; i < lengthOf(lods_distances); ++i) {
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, StaticString<32>("lod", i, "_distance"), &lods_distances[i]);
		}
	}

	void load(const Path& path, StudioApp& app) {
		if (lua_State* L = app.getAssetCompiler().getMeta(path)) {
			deserialize(L, path);
			lua_close(L);
		}
	}

	float scale = 1.f;
	float culling_scale = 1.f;
	bool split = false;
	bool create_impostor = false;
	bool bake_impostor_normals = false;
	bool bake_vertex_ao = false;
	bool use_mikktspace = false;
	bool force_skin = false;
	bool import_vertex_colors = false;
	bool vertex_color_is_ao = false;
	bool ignore_animations = false;
	u8 autolod_mask = 0;
	u32 lod_count = 1;
	float anim_rotation_error = 1.f;
	float anim_translation_error = 1.f;
	float autolod_coefs[4] = { 0.75f, 0.5f, 0.25f, 0.125f };
	float lods_distances[4] = { 10'000, 0, 0, 0 };
	Animation::Flags root_motion_flags = Animation::Flags::NONE;
	FBXImporter::ImportConfig::Origin origin = FBXImporter::ImportConfig::Origin::SOURCE;
	FBXImporter::ImportConfig::Physics physics = FBXImporter::ImportConfig::Physics::NONE;
	Array<FBXImporter::ImportConfig::Clip> clips;
	String root_motion_bone;
	Path skeleton;
};

}