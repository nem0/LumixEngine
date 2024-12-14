#pragma once

#include "core/allocator.h"
#include "core/stream.h"
#include "core/string.h"
#include "core/tokenizer.h"

namespace Lumix {

struct ModelMeta {
	struct Clip {
		StaticString<64> name;
		u32 from_frame;
		u32 to_frame;
	};

	enum class Physics : i32 {
		NONE,
		CONVEX,
		TRIMESH
	};

	enum class Origin : i32 {
		SOURCE, // keep vertex data as is
		CENTER, // center all meshes as a group
		BOTTOM, // same as center, but don't change Y coordinate
	};

	static const char* toString(Physics value) {
		switch (value) {
			case Physics::TRIMESH: return "trimesh";
			case Physics::CONVEX: return "convex";
			case Physics::NONE: return "none";
		}
		ASSERT(false);
		return "none";
	}

	static const char* toString(Origin value) {
		switch (value) {
			case Origin::SOURCE: return "source";
			case Origin::BOTTOM: return "bottom";
			case Origin::CENTER: return "center";
		}
		ASSERT(false);
		return "none";
	}

	static const char* toUIString(Physics value) {
		switch (value) {
			case Physics::TRIMESH: return "Triangle mesh";
			case Physics::CONVEX: return "Convex";
			case Physics::NONE: return "None";
		}
		ASSERT(false);
		return "none";
	}

	static const char* toUIString(Origin value) {
		switch (value) {
			case Origin::SOURCE: return "Keep";
			case Origin::BOTTOM: return "Bottom";
			case Origin::CENTER: return "Center";
		}
		ASSERT(false);
		return "none";
	}

	ModelMeta(IAllocator& allocator) : clips(allocator), root_motion_bone(allocator) {}

	void serialize(OutputMemoryStream& blob, const Path& path) {
		if (physics != Physics::NONE) blob << "\nphysics = \"" << toString(physics) << "\"";
		if (origin != Origin::SOURCE) blob << "\norigin = \"" << toString(origin) << "\"";
		blob << "\nlod_count = " << lod_count;

		#define WRITE_BOOL(id, default_value) \
			if ((id) != (default_value)) { \
				blob << "\n" << #id << (id ? " = true" : " = false"); \
			}

		#define WRITE_VALUE(id, default_value) \
			if ((id) != (default_value)) { blob << "\n" << #id << " = " << id; }
		
		WRITE_BOOL(create_prefab_with_physics, false);
		WRITE_BOOL(create_impostor, false);
		WRITE_BOOL(use_mikktspace, false);
		WRITE_BOOL(force_recompute_normals, false);
		WRITE_BOOL(force_recompute_tangents, false);
		WRITE_BOOL(force_skin, false);
		WRITE_BOOL(bake_vertex_ao, false);
		WRITE_BOOL(bake_impostor_normals, false);
		WRITE_BOOL(split, false);
		WRITE_BOOL(use_specular_as_roughness, true);
		WRITE_BOOL(use_specular_as_metallic, false);
		WRITE_BOOL(import_vertex_colors, false);
		WRITE_BOOL(vertex_color_is_ao, false);
		WRITE_BOOL(ignore_animations, false);
		WRITE_BOOL(ignore_material_colors, false);
		WRITE_VALUE(min_bake_vertex_ao, 0.f);
		WRITE_VALUE(anim_translation_error, 1.f);
		WRITE_VALUE(anim_rotation_error, 1.f);
		if (scene_scale != 1.f) blob << "\nscale = " << scene_scale;
		WRITE_VALUE(culling_scale, 1.f);
		WRITE_VALUE(root_motion_flags, Animation::Flags::NONE);

		#undef WRITE_BOOL
		#undef WRITE_VALUE

		if (root_motion_bone.length() > 0) blob << "\nroot_motion_bone = \"" << root_motion_bone << "\"";
		if (!skeleton.isEmpty()) {
			StringView dir = Path::getDir(ResourcePath::getResource(path));
			if (!dir.empty() && startsWith(skeleton, dir)) {
				blob << "\nskeleton_rel = \"" << skeleton.c_str() + dir.size();
			}
			else {
				blob << "\nskeleton = \"" << skeleton;
			}
			blob << "\"";
		}

		if (!clips.empty()) {
			blob << "\nclips = [";
			for (const Clip& clip : clips) {
				blob << "\n\n{";
				blob << "\n\n\nname = \"" << clip.name << "\",";
				blob << "\n\n\nfrom_frame = " << clip.from_frame << ",";
				blob << "\n\n\nto_frame = " << clip.to_frame;
				blob << "\n\n},";
			}
			blob << "\n]";
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

	void deserialize(StringView content, const Path& path) {
		autolod_coefs[0] = -1;
		autolod_coefs[1] = -1;
		autolod_coefs[2] = -1;
		autolod_coefs[3] = -1;
		StringView tmp_root_motion_bone, tmp_skeleton, tmp_skeleton_rel, tmp_physics, tmp_origin, tmp_clips;
		const ParseItemDesc descs[] = {
			{ "root_motion_flags", (i32*)&root_motion_flags },
			{ "use_mikktspace", &use_mikktspace },
			{ "force_recompute_normals", &force_recompute_normals },
			{ "force_recompute_tangents", &force_recompute_tangents },
			{ "force_skin", &force_skin },
			{ "anim_rotation_error", &anim_rotation_error },
			{ "anim_translation_error", &anim_translation_error },
			{ "scale", &scene_scale },
			{ "culling_scale", &culling_scale },
			{ "split", &split },
			{ "bake_impostor_normals", &bake_impostor_normals },
			{ "bake_vertex_ao", &bake_vertex_ao },
			{ "min_bake_vertex_ao", &min_bake_vertex_ao },
			{ "create_impostor", &create_impostor },
			{ "import_vertex_colors", &import_vertex_colors },
			{ "use_specular_as_roughness", &use_specular_as_roughness },
			{ "use_specular_as_metallic", &use_specular_as_metallic },
			{ "ignore_animations", &ignore_animations },
			{ "ignore_material_colors", &ignore_material_colors },
			{ "vertex_color_is_ao", &vertex_color_is_ao },
			{ "lod_count", &lod_count },
			{ "create_prefab_with_physics", &create_prefab_with_physics },
			{ "autolod0", &autolod_coefs[0] },
			{ "autolod1", &autolod_coefs[1] },
			{ "autolod2", &autolod_coefs[2] },
			{ "autolod3", &autolod_coefs[3] },
			{ "lod0_distance", &lods_distances[0] },
			{ "lod1_distance", &lods_distances[1] },
			{ "lod2_distance", &lods_distances[2] },
			{ "lod3_distance", &lods_distances[3] },
			{ "root_motion_bone", &tmp_root_motion_bone },
			{ "skeleton", &tmp_skeleton },
			{ "skeleton_rel", &tmp_skeleton_rel },
			{ "physics", &tmp_physics },
			{ "origin", &tmp_origin },
			{ "clips", &tmp_clips, true },
		};
		if (!parse(content, path.c_str(), descs)) return;

		if (autolod_coefs[0] >= 0) autolod_mask |= 1;
		if (autolod_coefs[1] >= 0) autolod_mask |= 2;
		if (autolod_coefs[2] >= 0) autolod_mask |= 4;
		if (autolod_coefs[3] >= 0) autolod_mask |= 8;

		root_motion_bone = tmp_root_motion_bone;
		if (!tmp_skeleton.empty()) skeleton = tmp_skeleton;
		if (!tmp_skeleton_rel.empty()) {
			StringView dir = Path::getDir(ResourcePath::getResource(path));
			skeleton = Path(dir, "/", tmp_skeleton_rel);
		}
		if (equalIStrings(tmp_physics, "trimesh")) physics = Physics::TRIMESH;
		else if (equalIStrings(tmp_physics, "convex")) physics = Physics::CONVEX;
		else physics = Physics::NONE;

		if (equalIStrings(tmp_origin, "center")) origin = Origin::CENTER;
		else if (equalIStrings(tmp_origin, "bottom")) origin = Origin::BOTTOM;
		else origin = Origin::SOURCE;

		clips.clear();	
		if (!tmp_clips.empty()) {
			Tokenizer t(StringView(content.begin, tmp_clips.end), path.c_str());
			t.cursor = tmp_clips.begin;
			Tokenizer::Token token = t.nextToken();
			ASSERT(token && token.value[0] == '[');
			for (;;) {
				token = t.nextToken();
				if (!token) return;
				if (token == "]") break;
				if (token != "{") {
					logError(t.filename, "(", t.getLine(), "): expected ']' or '{', got ", token.value);
					t.logErrorPosition(token.value.begin);
					return;
				}

				Clip& clip = clips.emplace();
				for (;;) {
					token = t.nextToken();
					if (!token) return;
					if (token == "}") break;
					if (!t.consume("=")) return;
					if (token == "name") {
						StringView name;
						if (!t.consume(name)) return;
						clip.name = name;
					}
					else if (token == "from_frame") {
						if (!t.consume(clip.from_frame)) return;
					}
					else if (token == "to_frame") {
						if (!t.consume(clip.to_frame)) return;
					}
					else {
						logError(t.filename, "(", t.getLine(), "): unknown token ", token.value);
						t.logErrorPosition(token.value.begin);
						return;
					}
					token = t.nextToken();
					if (!token) return;
					if (token == "}") break;
					if (token != ",") {
						logError(t.filename, "(", t.getLine(), "): expected '}' or ',', got ", token.value);
						t.logErrorPosition(token.value.begin);
						return;
					}
				}
				
				token = t.nextToken();
				if (!token) return;
				if (token == "]") break;
				if (token != ",") {
					logError(t.filename, "(", t.getLine(), "): expected ']' or ',', got ", token.value);
					t.logErrorPosition(token.value.begin);
					return;
				}
			}
		}
	}

	void load(const Path& path, StudioApp& app) {
		OutputMemoryStream blob(app.getAllocator());
		if (app.getAssetCompiler().getMeta(path, blob)) {
			StringView sv((const char*)blob.data(), (u32)blob.size());
			deserialize(sv, path);
		}
	}

	Path skeleton;
	bool force_recompute_normals = false;
	bool force_recompute_tangents = false;
	bool use_mikktspace = false;
	bool import_vertex_colors = false;
	bool bake_vertex_ao = false;
	bool use_specular_as_roughness = true;
	bool use_specular_as_metallic = false;
	bool vertex_color_is_ao = false;
	bool create_impostor = false;
	u32 lod_count = 1;
	float min_bake_vertex_ao = 0.f;
	float anim_translation_error = 1.f;
	float anim_rotation_error = 1.f;
	float culling_scale = 1.f;
	float scene_scale = 1.f;
	Origin origin = Origin::SOURCE;
	Physics physics = Physics::NONE;
	float lods_distances[4] = { 10'000, 0, 0, 0 };
	float autolod_coefs[4] = { 0.75f, 0.5f, 0.25f, 0.125f };
	u8 autolod_mask = 0;
	Animation::Flags root_motion_flags = Animation::Flags::NONE;
	
	Array<Clip> clips;
	String root_motion_bone;

	bool bake_impostor_normals = false;
	bool split = false;
	bool force_skin = false;
	bool ignore_animations = false;
	bool ignore_material_colors = false;
	bool create_prefab_with_physics = false;
};

}