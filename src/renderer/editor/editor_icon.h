#pragma once

#include "engine/black.h.h"

#include "core/hash_map.h"
#include "core/math.h"


namespace black
{

template <typename T> struct Array;
template <typename T> struct UniquePtr;

struct EditorIcons
{
	enum class IconType {
		PHYSICAL_CONTROLLER,
		CAMERA,
		LIGHT,
		TERRAIN,
		ENTITY,
		CURVE_DECAL,

		COUNT
	};

	struct Icon {
		EntityRef entity;
		IconType type;
		float scale;
	};

	struct Hit {
		EntityPtr entity;
		float t;
	};

	static UniquePtr<EditorIcons> create(struct WorldEditor& editor, struct RenderModule& module);

	virtual ~EditorIcons() {}

	virtual void computeScales() = 0;
	virtual Matrix getIconMatrix(const Icon& icon, const Matrix& camera_matrix, const DVec3& vp_pos, bool is_ortho, float ortho_size) const = 0;
	virtual const struct Model* getModel(IconType type) const = 0;
	virtual const HashMap<EntityRef, Icon>& getIcons() const = 0;
	virtual Hit raycast(const DVec3& origin, const Vec3& dir) = 0;
	virtual void refresh() = 0;
};


}