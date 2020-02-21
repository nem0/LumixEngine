#include "editor/gizmo.h"
#include "engine/crt.h"
#include "engine/geometry.h"
#include "engine/math.h"
#include "engine/os.h"
#include "engine/universe.h"
#include "render_interface.h"


namespace Lumix
{


static const float INFLUENCE_DISTANCE = 0.3f;
static const u32 X_COLOR = 0xff6363cf;
static const u32 Y_COLOR = 0xff63cf63;
static const u32 Z_COLOR = 0xffcf6363;
static const u32 SELECTED_COLOR = 0xff63cfcf;


enum class Axis : u32
{
	NONE,
	X,
	Y,
	Z,
	XY,
	XZ,
	YZ
};


enum class Pivot
{
	CENTER,
	OBJECT_PIVOT
};


enum class Mode : u32
{
	ROTATE,
	TRANSLATE,
	SCALE,

	COUNT
};


enum class CoordSystem
{
	LOCAL,
	WORLD
};


struct GizmoImpl final : Gizmo
{
	static const int MAX_GIZMOS = 16;
	

	explicit GizmoImpl(WorldEditor& editor)
		: m_editor(editor)
		, m_mode(Mode::TRANSLATE)
		, m_pivot(Pivot::CENTER)
		, m_coord_system(CoordSystem::LOCAL)
		, m_is_autosnap_down(false)
		, m_count(0)
		, m_transform_axis(Axis::X)
		, m_active(-1)
		, m_is_step(false)
		, m_rel_accum(0, 0)
		, m_is_dragging(false)
		, m_mouse_pos(0, 0)
		, m_offset(0, 0, 0)
	{
		m_steps[int(Mode::TRANSLATE)] = 10;
		m_steps[int(Mode::ROTATE)] = 45;
		m_steps[int(Mode::SCALE)] = 1;
		editor.universeDestroyed().bind<&GizmoImpl::onUniverseDestroyed>(this);
	}


	~GizmoImpl()
	{
		m_editor.universeDestroyed().unbind<&GizmoImpl::onUniverseDestroyed>(this);
	}


	void onUniverseDestroyed()
	{
		m_count = 0;
	}


	RigidTransform getTransform(EntityRef entity) const
	{
		RigidTransform res;
		const Universe* universe = m_editor.getUniverse();
		switch (m_pivot) {
			case Pivot::OBJECT_PIVOT :
				res = universe->getTransform(entity).getRigidPart();
				res.pos += res.rot * m_offset;
				break;
		
			case Pivot::CENTER: {
				const Transform tr = universe->getTransform(entity);
				const Vec3 center = m_editor.getRenderInterface()->getModelCenter(entity);
				res = tr.getRigidPart();
				res.pos = res.pos + res.rot * (center * tr.scale);
				break;
			}
			default:
				ASSERT(false);
				break;
		}

		if (m_coord_system == CoordSystem::WORLD) {
			res.rot.set(0, 0, 0, 0);
		}
		return res;
	}


	static float getScale(const DVec3& camera_pos, float fov, const DVec3& pos, bool is_ortho)
	{
		if (is_ortho) return 2;
		float scale = tanf(fov * 0.5f) * (pos - camera_pos).toFloat().length() * 2;
		return scale / 10;
	}


	struct DataPtrs {
		u16* indices;
		RenderData::Vertex* vertices;
	};


	static DataPtrs reserve(RenderData* data, const Matrix& mtx, bool lines, u32 vertices_count, u32 indices_count)
	{
		data->cmds.push({mtx, lines, (u32)data->indices.size(), indices_count, (u32)data->vertices.size(), vertices_count});
		data->vertices.resize(data->vertices.size() + vertices_count);
		data->indices.resize(data->indices.size() + indices_count);

		return { data->indices.end() - indices_count, data->vertices.end() - vertices_count };
	}


	void update(const Viewport& vp) override
	{
		RenderInterface* ri = m_editor.getRenderInterface();
		for (int i = 0; i < m_count; ++i) {
			const RigidTransform gizmo_tr = getTransform(m_entities[i]);
			const Vec2 p = m_editor.getView().getViewport().worldToScreenPixels(gizmo_tr.pos);
			switch (m_mode) {
				case Mode::SCALE: break;
				case Mode::TRANSLATE:
					if (m_is_dragging) {
						const DVec3 intersection = getMousePlaneIntersection(m_editor.getView().getMousePos(), gizmo_tr, m_transform_axis);
						const Vec3 delta_vec = (intersection - m_start_axis_point).toFloat();

						StaticString<128> tmp("", delta_vec.x, "; ", delta_vec.y, "; ", delta_vec.z);
						ri->addText2D(p.x + 31, p.y + 31, 0xff000000, tmp);
						ri->addText2D(p.x + 30, p.y + 30, 0xffffFFFF, tmp);
					}

					break;
				case Mode::ROTATE:
					if (m_is_dragging && m_active == i) {
						float angle_degrees = radiansToDegrees(m_angle_accum);
						StaticString<128> tmp("", angle_degrees, " deg");
						Vec2 screen_delta = (m_start_mouse_pos - p).normalized();
						Vec2 text_pos = m_start_mouse_pos + screen_delta * 15;
						ri->addText2D(text_pos.x + 1, text_pos.y + 1, 0xff000000, tmp);
						ri->addText2D(text_pos.x, text_pos.y, 0xffffFFFF, tmp);
					}
					break;
				default: ASSERT(false); break;
			}
		}
	}

	
	void renderTranslateGizmo(const RigidTransform& gizmo_transform,
		bool is_active,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		RenderData* data) const
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		DVec3 entity_pos = gizmo_transform.pos;
		float scale = getScale(camera_pos, fov, entity_pos, is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : (camera_pos - entity_pos).toFloat();
		Matrix mtx = gizmo_transform.rot.toMatrix() * scale_mtx;
		mtx.translate((gizmo_transform.pos - camera_pos).toFloat());

		{
			const DataPtrs ptrs = reserve(data, mtx, true, 6, 6);
			u16* indices = ptrs.indices;
			RenderData::Vertex* vertices = ptrs.vertices;

			vertices[0].position = Vec3(0, 0, 0);
			vertices[0].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
			indices[0] = 0;
			vertices[1].position = Vec3(1, 0, 0);
			vertices[1].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
			indices[1] = 1;
			vertices[2].position = Vec3(0, 0, 0);
			vertices[2].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
			indices[2] = 2;
			vertices[3].position = Vec3(0, 1, 0);
			vertices[3].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
			indices[3] = 3;
			vertices[4].position = Vec3(0, 0, 0);
			vertices[4].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
			indices[4] = 4;
			vertices[5].position = Vec3(0, 0, 1);
			vertices[5].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
			indices[5] = 5;
		}

		if (dotProduct(mtx.getXVector(), to_entity_dir) < 0) mtx.setXVector(-mtx.getXVector());
		if (dotProduct(mtx.getYVector(), to_entity_dir) < 0) mtx.setYVector(-mtx.getYVector());
		if (dotProduct(mtx.getZVector(), to_entity_dir) < 0) mtx.setZVector(-mtx.getZVector());

		const DataPtrs ptrs = reserve(data, mtx, false, 9, 9);
		u16* indices = ptrs.indices;
		RenderData::Vertex* vertices = ptrs.vertices;

		vertices[0].position = Vec3(0, 0, 0);
		vertices[0].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[0] = 0;
		vertices[1].position = Vec3(0.5f, 0, 0);
		vertices[1].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[1] = 1;
		vertices[2].position = Vec3(0, 0.5f, 0);
		vertices[2].color = transform_axis == Axis::XY ? SELECTED_COLOR : Z_COLOR;
		indices[2] = 2;

		vertices[3].position = Vec3(0, 0, 0);
		vertices[3].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[3] = 3;
		vertices[4].position = Vec3(0, 0.5f, 0);
		vertices[4].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[4] = 4;
		vertices[5].position = Vec3(0, 0, 0.5f);
		vertices[5].color = transform_axis == Axis::YZ ? SELECTED_COLOR : X_COLOR;
		indices[5] = 5;

		vertices[6].position = Vec3(0, 0, 0);
		vertices[6].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[6] = 6;
		vertices[7].position = Vec3(0.5f, 0, 0);
		vertices[7].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[7] = 7;
		vertices[8].position = Vec3(0, 0, 0.5f);
		vertices[8].color = transform_axis == Axis::XZ ? SELECTED_COLOR : Y_COLOR;
		indices[8] = 8;
	}


	void renderScaleGizmo(const RigidTransform& gizmo_tr,
		bool is_active,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		RenderData* data) const
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		const float scale = getScale(camera_pos, fov, gizmo_tr.pos, is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Matrix mtx = gizmo_tr.rot.toMatrix() * scale_mtx;
		mtx.translate((gizmo_tr.pos - camera_pos).toFloat());

		{
			const DataPtrs ptrs = reserve(data, mtx, true, 6, 6);
			u16* indices = ptrs.indices;
			RenderData::Vertex* vertices = ptrs.vertices;

			vertices[0].position = Vec3(0, 0, 0);
			vertices[0].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
			indices[0] = 0;
			vertices[1].position = Vec3(1, 0, 0);
			vertices[1].color = transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR;
			indices[1] = 1;
			vertices[2].position = Vec3(0, 0, 0);
			vertices[2].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
			indices[2] = 2;
			vertices[3].position = Vec3(0, 1, 0);
			vertices[3].color = transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR;
			indices[3] = 3;
			vertices[4].position = Vec3(0, 0, 0);
			vertices[4].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
			indices[4] = 4;
			vertices[5].position = Vec3(0, 0, 1);
			vertices[5].color = transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR;
			indices[5] = 5;
		}

		auto renderCube = [](RenderData* data, u32 color, const Matrix& mtx, const Vec3& pos) 
		{
			const DataPtrs ptrs = reserve(data, mtx, false, 8, 36);
			u16* indices = ptrs.indices;
			RenderData::Vertex* vertices = ptrs.vertices;

			for (int i = 0; i < 8; ++i) vertices[i].color = color;

			vertices[0].position = pos + Vec3(-0.1f, -0.1f, -0.1f);
			vertices[1].position = pos + Vec3(0.1f, -0.1f, -0.1f);
			vertices[2].position = pos + Vec3(0.1f, -0.1f, 0.1f);
			vertices[3].position = pos + Vec3(-0.1f, -0.1f, 0.1f);

			vertices[4].position = pos + Vec3(-0.1f, 0.1f, -0.1f);
			vertices[5].position = pos + Vec3(0.1f, 0.1f, -0.1f);
			vertices[6].position = pos + Vec3(0.1f, 0.1f, 0.1f);
			vertices[7].position = pos + Vec3(-0.1f, 0.1f, 0.1f);

			u16 indices_tmp[36] =
			{
				0, 1, 2,
				0, 2, 3,
				4, 6, 5,
				4, 7, 6,
				0, 4, 5,
				0, 5, 1,
				2, 6, 7,
				2, 7, 3,
				0, 3, 7,
				0, 7, 4,
				1, 2, 6,
				1, 6, 5
			};

			memcpy(indices, indices_tmp, sizeof(indices_tmp));
		};
		renderCube(data, transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR, mtx, Vec3(1, 0, 0));
		renderCube(data, transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR, mtx, Vec3(0, 1, 0));
		renderCube(data, transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR, mtx, Vec3(0, 0, 1));
		
	}

	void renderQuarterRing(RenderData* data, const Matrix& mtx, const Vec3& a, const Vec3& b, u32 color) const
	{
		const DataPtrs ptrs = reserve(data, mtx, false, 25*6, 25*6);
		u16* indices = ptrs.indices;
		RenderData::Vertex* vertices = ptrs.vertices;

		const float ANGLE_STEP = degreesToRadians(1.0f / 100.0f * 360.0f);
		Vec3 n = crossProduct(a, b) * 0.05f;
		int offset = -1;
		for (int i = 0; i < 25; ++i) {
			float angle = i * ANGLE_STEP;
			float s = sinf(angle);
			float c = cosf(angle);
			float sn = sinf(angle + ANGLE_STEP);
			float cn = cosf(angle + ANGLE_STEP);

			Vec3 p0 = a * s + b * c - n * 0.5f;
			Vec3 p1 = a * sn + b * cn - n * 0.5f;

			++offset;
			vertices[offset].position = p0;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p0 + n;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p1 + n;
			vertices[offset].color = color;
			indices[offset] = offset;

			++offset;
			vertices[offset].position = p0 + n;
			vertices[offset].color = color;
			indices[offset] = offset;
		}

		{
			const int GRID_SIZE = 5;
			const DataPtrs ptrs = reserve(data, mtx, true, (GRID_SIZE + 1) * 4, (GRID_SIZE + 1) * 4);
			u16* indices = ptrs.indices;
			RenderData::Vertex* vertices = ptrs.vertices;

			offset = -1;
			for (int i = 0; i <= GRID_SIZE; ++i) {
				float t = 1.0f / GRID_SIZE * i;
				float ratio = sinf(acosf(t));

				++offset;
				vertices[offset].position = a * t;
				vertices[offset].color = color;
				indices[offset] = offset;

				++offset;
				vertices[offset].position = a * t + b * ratio;
				vertices[offset].color = color;
				indices[offset] = offset;

				++offset;
				vertices[offset].position = b * t + a * ratio;
				vertices[offset].color = color;
				indices[offset] = offset;

				++offset;
				vertices[offset].position = b * t;
				vertices[offset].color = color;
				indices[offset] = offset;
			}
		}
	}


	void renderArc(RenderData* data, const Vec3& pos, const Vec3& n, const Vec3& origin, float angle, u32 color) const
	{
		
		const int count = clamp(int(fabs(angle) / 0.1f), 1, 50);
		const DataPtrs ptrs = reserve(data, Matrix::IDENTITY, false, count + 2, count * 3);
		u16* indices = ptrs.indices;
		RenderData::Vertex* vertices = ptrs.vertices;

		const Vec3 side = crossProduct(n.normalized(), origin);

		vertices[0] = { pos, color };
		for (int i = 0; i <= count; ++i)
		{
			float a = angle / count * i;

			Vec3 p = pos + origin * cosf(a) + side * sinf(a);
			vertices[i + 1] = { p, color };
		}
		for (int i = 0; i < count; ++i)
		{
			indices[i * 3 + 0] = 0;
			indices[i * 3 + 1] = i+1;
			indices[i * 3 + 2] = i + 2;
		}
	}


	void renderRotateGizmo(const RigidTransform& gizmo_tr,
		bool is_active,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		RenderData* data) const
	{
		Axis transform_axis = is_active ? m_transform_axis : Axis::NONE;
		Matrix scale_mtx = Matrix::IDENTITY;
		float scale = getScale(camera_pos, fov, gizmo_tr.pos, is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		Vec3 to_entity_dir = is_ortho ? camera_dir : (camera_pos - gizmo_tr.pos).toFloat();
		Matrix mtx = gizmo_tr.rot.toMatrix() * scale_mtx;
		mtx.translate((gizmo_tr.pos - camera_pos).toFloat());

		Vec3 right(1, 0, 0);
		Vec3 up(0, 1, 0);
		Vec3 dir(0, 0, 1);

		if (dotProduct(mtx.getXVector(), to_entity_dir) < 0) right = -right;
		if (dotProduct(mtx.getYVector(), to_entity_dir) < 0) up = -up;
		if (dotProduct(mtx.getZVector(), to_entity_dir) < 0) dir = -dir;

		if (!m_is_dragging || !is_active)
		{
			renderQuarterRing(data, mtx, right, up, transform_axis == Axis::Z ? SELECTED_COLOR : Z_COLOR);
			renderQuarterRing(data, mtx, up, dir, transform_axis == Axis::X ? SELECTED_COLOR : X_COLOR);
			renderQuarterRing(data, mtx, right, dir, transform_axis == Axis::Y ? SELECTED_COLOR : Y_COLOR);
		}
		else
		{
			Vec3 n;
			Vec3 axis1, axis2;
			switch (transform_axis)
			{
				case Axis::X:
					n = mtx.getXVector();
					axis1 = up;
					axis2 = dir;
					break;
				case Axis::Y:
					n = mtx.getYVector();
					axis1 = right;
					axis2 = dir;
					break;
				case Axis::Z:
					n = mtx.getZVector();
					axis1 = right;
					axis2 = up;
					break;
				default: ASSERT(false); break;
			}
			renderQuarterRing(data, mtx, axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(data, mtx, -axis1, axis2, SELECTED_COLOR);
			renderQuarterRing(data, mtx, -axis1, -axis2, SELECTED_COLOR);
			renderQuarterRing(data, mtx, axis1, -axis2, SELECTED_COLOR);

			const Vec3 origin = (m_start_plane_point - gizmo_tr.pos).toFloat().normalized();
			renderArc(data, (gizmo_tr.pos - camera_pos).toFloat(), n * scale, origin * scale, m_angle_accum, 0x8800a5ff);
		}
	}


	void clearEntities() override
	{
		m_active = -1;
		m_pivot = Pivot::CENTER;
		m_offset.set(0, 0, 0);
		m_count = 0;
	}


	bool isActive() const override
	{
		return m_transform_axis != Axis::NONE;
	}


	Axis collideTranslate(const RigidTransform& gizmo_tr,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const DVec3& origin,
		const Vec3& dir) const
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		const float scale = getScale(camera_pos, fov, gizmo_tr.pos, is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		const Vec3 to_entity_dir = is_ortho ? camera_dir : (camera_pos - gizmo_tr.pos).toFloat();
		const Matrix mtx = gizmo_tr.rot.toMatrix() * scale_mtx;
		const Vec3 pos = (gizmo_tr.pos - camera_pos).toFloat();

		Vec3 x = mtx.getXVector() * 0.5f;
		Vec3 y = mtx.getYVector() * 0.5f;
		Vec3 z = mtx.getZVector() * 0.5f;

		if (dotProduct(mtx.getXVector(), to_entity_dir) < 0) x = -x;
		if (dotProduct(mtx.getYVector(), to_entity_dir) < 0) y = -y;
		if (dotProduct(mtx.getZVector(), to_entity_dir) < 0) z = -z;

		const Vec3 rel_origin = (origin - camera_pos).toFloat();
		float t, tmin = FLT_MAX;
		bool hit = getRayTriangleIntersection(rel_origin, dir, pos, pos + x, pos + y, &t);
		Axis transform_axis = Axis::NONE;
		if (hit)
		{
			tmin = t;
			transform_axis = Axis::XY;
		}
		hit = getRayTriangleIntersection(rel_origin, dir, pos, pos + y, pos + z, &t);
		if (hit && t < tmin)
		{
			tmin = t;
			transform_axis = Axis::YZ;
		}
		hit = getRayTriangleIntersection(rel_origin, dir, pos, pos + x, pos + z, &t);
		if (hit && t < tmin)
		{
			transform_axis = Axis::XZ;
		}

		if (transform_axis != Axis::NONE)
		{
			return transform_axis;
		}

		float x_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getXVector());
		float y_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getYVector());
		float z_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getZVector());

		float influenced_dist = scale * INFLUENCE_DISTANCE;
		if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
		{
			return Axis::NONE;
		}

		if (x_dist < y_dist && x_dist < z_dist)
			return Axis::X;
		else if (y_dist < z_dist)
			return Axis::Y;
		return Axis::Z;
	}


	static Axis collideScale(const RigidTransform& gizmo_tr,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const DVec3& origin,
		const Vec3& dir)
	{
		Matrix scale_mtx = Matrix::IDENTITY;
		const float scale = getScale(camera_pos, fov, gizmo_tr.pos, is_ortho);
		scale_mtx.m11 = scale_mtx.m22 = scale_mtx.m33 = scale;

		const Matrix mtx = gizmo_tr.rot.toMatrix() * scale_mtx;
		const Vec3 pos = (gizmo_tr.pos - camera_pos).toFloat();
		const Vec3 rel_origin = (origin - camera_pos).toFloat();
		const float x_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getXVector());
		const float y_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getYVector());
		const float z_dist = getLineSegmentDistance(rel_origin, dir, pos, pos + mtx.getZVector());

		float influenced_dist = scale * INFLUENCE_DISTANCE;
		if (x_dist > influenced_dist && y_dist > influenced_dist && z_dist > influenced_dist)
		{
			return Axis::NONE;
		}

		if (x_dist < y_dist && x_dist < z_dist)
			return Axis::X;
		else if (y_dist < z_dist)
			return Axis::Y;
		return Axis::Z;
	}


	Axis collideRotate(const RigidTransform& gizmo_tr,
		const DVec3& camera_pos,
		const Vec3& camera_dir,
		float fov,
		bool is_ortho,
		const DVec3& origin,
		const Vec3& dir) const
	{
		const Vec3 pos = (gizmo_tr.pos - camera_pos).toFloat();
		const float scale = getScale(camera_pos, fov, gizmo_tr.pos, is_ortho);
		const Vec3 rel_origin = (origin - camera_pos).toFloat();

		Vec3 hit;
		if (getRaySphereIntersection(rel_origin, dir, pos, scale, hit)) {
			const Vec3 x = gizmo_tr.rot * Vec3(1, 0, 0);
			const float x_dist = fabsf(dotProduct(hit, x) - dotProduct(x, pos));

			const Vec3 y = gizmo_tr.rot * Vec3(0, 1, 0);
			const float y_dist = fabsf(dotProduct(hit, y) - dotProduct(y, pos));

			const Vec3 z = gizmo_tr.rot * Vec3(0, 0, 1);
			const float z_dist = fabsf(dotProduct(hit, z) - dotProduct(z, pos));

			float influence_dist = scale * 0.15f;
			if (x_dist > influence_dist && y_dist > influence_dist && z_dist > influence_dist)
			{
				return Axis::NONE;
			}

			if (x_dist < y_dist && x_dist < z_dist)
				return Axis::X;
			else if (y_dist < z_dist)
				return Axis::Y;
			else
				return Axis::Z;
		}
		return Axis::NONE;
	}


	void collide(const RigidTransform& gizmo_tr)
	{
		if (m_is_dragging) return;

		const Viewport& vp = m_editor.getView().getViewport();
		const Vec3 vp_dir = vp.rot * Vec3(0, 0, -1);
		DVec3 origin;
		Vec3 cursor_dir;
		vp.getRay(m_editor.getView().getMousePos(), origin, cursor_dir);
		
		Axis axis = Axis::NONE;
		switch(m_mode)
		{
			case Mode::TRANSLATE:
				axis = collideTranslate(gizmo_tr, vp.pos, vp_dir, vp.fov, vp.is_ortho, origin, cursor_dir);
				break;
			case Mode::ROTATE:
				axis = collideRotate(gizmo_tr, vp.pos, vp_dir, vp.fov, vp.is_ortho, origin, cursor_dir);
				break;
			case Mode::SCALE:
				axis = collideScale(gizmo_tr, vp.pos, vp_dir, vp.fov, vp.is_ortho, origin, cursor_dir);
				break;
			default:
				ASSERT(false);
				break;
		}
		if (axis != Axis::NONE)
		{
			m_transform_axis = axis;
			m_active = -1;
		}
	}


	void collide(const DVec3& camera_pos, const Vec3& camera_dir, float fov, bool is_ortho)
	{
		if (m_is_dragging) return;

		DVec3 origin;
		Vec3 cursor_dir;
		m_editor.getView().getViewport().getRay(m_editor.getView().getMousePos(), origin, cursor_dir);

		m_transform_axis = Axis::NONE;
		m_active = -1;
		for (int i = 0; i < m_count; ++i)
		{
			const RigidTransform gizmo_tr = getTransform((EntityRef)m_entities[i]);

			Axis axis = Axis::NONE;
			switch (m_mode)
			{
				case Mode::TRANSLATE:
					axis = collideTranslate(gizmo_tr, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				case Mode::ROTATE:
					axis = collideRotate(gizmo_tr, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				case Mode::SCALE:
					axis = collideScale(gizmo_tr, camera_pos, camera_dir, fov, is_ortho, origin, cursor_dir);
					break;
				default:
					ASSERT(false);
					break;
			}
			if (axis != Axis::NONE)
			{
				m_transform_axis = axis;
				m_active = i;
				return;
			}
		}
	}


	DVec3 getMousePlaneIntersection(const Vec2& mouse_pos, const RigidTransform& gizmo_tr, Axis transform_axis) const
	{
		const Viewport& vp = m_editor.getView().getViewport();
		DVec3 origin;
		Vec3 dir;
		vp.getRay(mouse_pos, origin, dir);
		bool is_two_axed = transform_axis == Axis::XZ || transform_axis == Axis::XY || transform_axis == Axis::YZ;
		if (is_two_axed)
		{
			Vec3 plane_normal;
			switch (transform_axis)
			{
				case Axis::XZ: plane_normal = gizmo_tr.rot * Vec3(0, 1, 0); break;
				case Axis::XY: plane_normal = gizmo_tr.rot * Vec3(0, 0, 1); break;
				case Axis::YZ: plane_normal = gizmo_tr.rot * Vec3(1, 0, 0); break;
				default: ASSERT(false); break;
			}
			float t;
			const Vec3 rel_origin = (origin - gizmo_tr.pos).toFloat();
			if (getRayPlaneIntersecion(rel_origin, dir, Vec3(0), plane_normal, t))
			{
				return origin + dir * t;
			}
			return origin;
		}
		Vec3 axis;
		switch (transform_axis)
		{
			case Axis::X: axis = gizmo_tr.rot * Vec3(1, 0, 0);; break;
			case Axis::Y: axis = gizmo_tr.rot * Vec3(0, 1, 0);; break;
			case Axis::Z: axis = gizmo_tr.rot * Vec3(0, 0, 1);; break;
			default: ASSERT(false); return DVec3(0);
		}
		DVec3 pos = gizmo_tr.pos;
		Vec3 normal = crossProduct(crossProduct(dir, axis), dir);
		float d = dotProduct((origin - pos).toFloat(), normal) / dotProduct(axis, normal);
		return pos + axis * d;
	}


	float computeRotateAngle(int relx, int rely)
	{
		if (relx == 0 && rely == 0) return 0;

		const RigidTransform tr = getTransform(m_entities[m_active]);
		Axis plane;
		Vec3 axis;
		switch (m_transform_axis)
		{
			case Axis::X: plane = Axis::YZ; axis = tr.rot * Vec3(1, 0, 0); break;
			case Axis::Y: plane = Axis::XZ; axis = tr.rot * Vec3(0, 1, 0); break;
			case Axis::Z: plane = Axis::XY; axis = tr.rot * Vec3(0, 0, 1); break;
			default: ASSERT(false); return 0;
		}

		const DVec3 pos = getMousePlaneIntersection(m_editor.getView().getMousePos(), tr, plane);
		const DVec3 start_pos = getMousePlaneIntersection(m_mouse_pos, tr, plane);
		const Vec3 delta = (pos - tr.pos).toFloat().normalized();
		const Vec3 start_delta = (start_pos - tr.pos).toFloat().normalized();
		
		const Vec3 side = crossProduct(axis, start_delta);

		const float y = clamp(dotProduct(delta, start_delta), -1.0f, 1.0f);
		const float x = clamp(dotProduct(delta, side), -1.0f, 1.0f);

		return atan2f(x, y);
		/*
		if (m_is_step)
		{
			m_rel_accum.x += relx;
			m_rel_accum.y += rely;
			if (m_rel_accum.x + m_rel_accum.y > 50)
			{
				m_rel_accum.x = m_rel_accum.y = 0;
				return degreesToRadians(float(getStep()));
			}
			else if (m_rel_accum.x + m_rel_accum.y < -50)
			{
				m_rel_accum.x = m_rel_accum.y = 0;
				return -degreesToRadians(float(getStep()));
			}
			else
			{
				return 0;
			}
		}
		return (relx + rely) / 100.0f;*/
	}


	static Axis getPlane(Axis axis)
	{
		switch (axis)
		{
			case Axis::X: return Axis::YZ;
			case Axis::Y: return Axis::XZ;
			case Axis::Z: return Axis::XY;
			default: return axis;
		}
	}


	void rotate()
	{
		if (m_active < 0) return;

		float relx = m_editor.getView().getMousePos().x - m_mouse_pos.x;
		float rely = m_editor.getView().getMousePos().y - m_mouse_pos.y;

		const Universe* universe = m_editor.getUniverse();
		Array<DVec3> new_positions(m_editor.getAllocator());
		Array<Quat> new_rotations(m_editor.getAllocator());
		const RigidTransform tr = getTransform(m_entities[m_active]);

		Vec3 axis;
		switch (m_transform_axis) {
			case Axis::X: axis = tr.rot * Vec3(1, 0, 0); break;
			case Axis::Y: axis = tr.rot * Vec3(0, 1, 0); break;
			case Axis::Z: axis = tr.rot * Vec3(0, 0, 1); break;
			default: ASSERT(false); break;
		}
		float angle = computeRotateAngle((int)relx, (int)rely);
		m_mouse_pos = m_editor.getView().getMousePos();

		m_angle_accum += angle;
		m_angle_accum = angleDiff(m_angle_accum, 0);

		if (m_editor.getSelectedEntities()[0] == m_entities[m_active]) {
			for (EntityRef e : m_editor.getSelectedEntities()) {
				DVec3 pos = universe->getPosition(e);
				const Quat old_rot = universe->getRotation(e);
				Quat new_rot = Quat(axis, angle) * old_rot;
				new_rot.normalize();
				new_rotations.push(new_rot);
				Vec3 pdif = (pos - tr.pos).toFloat();
				pdif = new_rot.rotate(old_rot.conjugated().rotate(pdif));
				pos = tr.pos + pdif;

				new_positions.push(pos);
			}
			m_editor.setEntitiesPositionsAndRotations(&m_editor.getSelectedEntities()[0],
				&new_positions[0],
				&new_rotations[0],
				new_positions.size());
		}
		else {
			Quat old_rot = universe->getRotation(m_entities[m_active]);
			Quat new_rot = Quat(axis, angle) * old_rot;
			new_rot.normalize();
			new_rotations.push(new_rot);
			m_editor.setEntitiesRotations(&m_entities[m_active], &new_rotations[0], 1);
		}
	}


	void scale()
	{
		RigidTransform gizmo_tr = getTransform(m_entities[m_active]);
		const DVec3 intersection = getMousePlaneIntersection(m_editor.getView().getMousePos(), gizmo_tr, m_transform_axis);
		const Vec3 delta_vec = (intersection - m_transform_point).toFloat();
		float delta = delta_vec.length();
		const Vec3 entity_to_intersection = (intersection - gizmo_tr.pos).toFloat();
		if (dotProduct(delta_vec, entity_to_intersection) < 0) delta = -delta;
		if (!m_is_step || delta > float(getStep()))
		{
			if (m_is_step) delta = float(getStep());

			Array<float> new_scales(m_editor.getAllocator());
			if (m_entities[m_active] == m_editor.getSelectedEntities()[0])
			{
				for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
				{
					float scale = m_editor.getUniverse()->getScale(m_editor.getSelectedEntities()[i]);
					scale += delta;
					new_scales.push(scale);
				}
				m_editor.setEntitiesScales(&m_editor.getSelectedEntities()[0], &new_scales[0], new_scales.size());
			}
			else
			{
				float scale = m_editor.getUniverse()->getScale(m_entities[m_active]);
				scale += delta;
				new_scales.push(scale);
				m_editor.setEntitiesScales(&m_entities[m_active], &new_scales[0], 1);
			}

			m_transform_point = intersection;
		}
	}


	void translate()
	{
		const RigidTransform tr = getTransform(m_entities[m_active]);
		const DVec3 intersection = getMousePlaneIntersection(m_editor.getView().getMousePos(), tr, m_transform_axis);
		Vec3 delta = (intersection - m_transform_point).toFloat();
		if (!m_is_step || delta.length() > float(getStep()))
		{
			if (m_is_step) delta = delta.normalized() * float(getStep());

			Array<DVec3> new_positions(m_editor.getAllocator());
			if (m_entities[m_active] == m_editor.getSelectedEntities()[0])
			{
				for (int i = 0, ci = m_editor.getSelectedEntities().size(); i < ci; ++i)
				{
					DVec3 pos = m_editor.getUniverse()->getPosition(m_editor.getSelectedEntities()[i]);
					pos += delta;
					new_positions.push(pos);
				}
				m_editor.setEntitiesPositions(
					&m_editor.getSelectedEntities()[0], &new_positions[0], new_positions.size());
				if (m_is_autosnap_down) m_editor.snapDown();
			}
			else
			{
				DVec3 pos = m_editor.getUniverse()->getPosition(m_entities[m_active]);
				pos += delta;
				new_positions.push(pos);
				m_editor.setEntitiesPositions(&m_entities[m_active], &new_positions[0], 1);
			}

			m_transform_point = intersection;
		}
	}


	void transform()
	{
		if (m_active >= 0 && m_editor.getView().isMouseClick(OS::MouseButton::LEFT))
		{
			const RigidTransform gizmo_tr = getTransform(m_entities[m_active]);
			m_transform_point = getMousePlaneIntersection(m_editor.getView().getMousePos(), gizmo_tr, m_transform_axis);
			m_start_axis_point = m_transform_point;
			m_start_plane_point = getMousePlaneIntersection(m_editor.getView().getMousePos(), gizmo_tr, getPlane(m_transform_axis));
			m_start_mouse_pos = m_editor.getView().getMousePos();
			m_angle_accum = 0;
			m_is_dragging = true;
			m_mouse_pos = m_editor.getView().getMousePos();
		}
		else if (!m_editor.getView().isMouseDown(OS::MouseButton::LEFT))
		{
			m_is_dragging = false;
		}
		if (!m_is_dragging || m_active < 0) return;

		switch (m_mode)
		{
			case Mode::ROTATE: rotate(); break;
			case Mode::TRANSLATE: translate(); break;
			case Mode::SCALE: scale(); break;
			default: ASSERT(false); break;
		}
	}


	void render(const RigidTransform& gizmo_tr, bool is_active, const Viewport& vp, RenderData* data) const
	{
		const Vec3 vp_dir = vp.rot * Vec3(0, 0, -1);

		switch (m_mode)
		{
			case Mode::TRANSLATE:
				renderTranslateGizmo(gizmo_tr, is_active, vp.pos, vp_dir, vp.fov, vp.is_ortho, data);
				break;
			case Mode::ROTATE:
				renderRotateGizmo(gizmo_tr, is_active, vp.pos, vp_dir, vp.fov, vp.is_ortho, data);
				break;
			case Mode::SCALE:
				renderScaleGizmo(gizmo_tr, is_active, vp.pos, vp_dir, vp.fov, vp.is_ortho, data);
				break;
			default:
				ASSERT(false);
				break;
		}
	}

	
	void getRenderData(RenderData* data, const Viewport& vp) override
	{
		collide(vp.pos, vp.rot * Vec3(0, 0, -1), vp.fov, vp.is_ortho);
		transform();

		for (int i = 0; i < m_count; ++i) {
			const RigidTransform gizmo_tr = getTransform(m_entities[i]);
			render(gizmo_tr, m_active == i, vp, data);
		}

		m_count = 0;
	}


	Vec3 getOffset() const override
	{
		return m_offset;
	}


	void setOffset(const Vec3& offset) override
	{
		m_pivot = Pivot::OBJECT_PIVOT;
		m_offset = offset;
	}


	void add(EntityRef entity) override
	{
		if (m_count >= MAX_GIZMOS) return;

		m_entities[m_count] = entity;
		++m_count;
	}


	void setTranslateMode() override
	{
		m_mode = Mode::TRANSLATE;
	}

	void setRotateMode() override
	{
		m_mode = Mode::ROTATE;
	}

	void setScaleMode() override
	{
		m_mode = Mode::SCALE;
	}

	void setPivotCenter() override { m_pivot = Pivot::CENTER; m_offset.set(0, 0, 0); }
	void setPivotOrigin() override { m_pivot = Pivot::OBJECT_PIVOT; }
	bool isPivotCenter() const override { return m_pivot == Pivot::CENTER; }
	bool isPivotOrigin() const override { return m_pivot == Pivot::OBJECT_PIVOT; }
	void setGlobalCoordSystem() override { m_coord_system = CoordSystem::WORLD; }
	void setLocalCoordSystem() override { m_coord_system = CoordSystem::LOCAL; }
	bool isLocalCoordSystem() const override { return m_coord_system == CoordSystem::LOCAL; }
	bool isGlobalCoordSystem() const override { return m_coord_system == CoordSystem::WORLD; }
	int getStep() const override { return m_steps[(int)m_mode]; }
	void enableStep(bool enable) override { m_is_step = enable; }
	void setStep(int step) override { m_steps[(int)m_mode] = step; }
	bool isAutosnapDown() const override { return m_is_autosnap_down; }
	void setAutosnapDown(bool snap) override { m_is_autosnap_down = snap; }
	bool isTranslateMode() const override { return m_mode == Mode::TRANSLATE; }
	bool isScaleMode() const override { return m_mode == Mode::SCALE; }
	bool isRotateMode() const override { return m_mode == Mode::ROTATE; }

	Pivot m_pivot;
	CoordSystem m_coord_system;
	int m_steps[(int)Mode::COUNT];
	Mode m_mode;
	Axis m_transform_axis;
	bool m_is_autosnap_down;
	WorldEditor& m_editor;
	DVec3 m_transform_point;
	DVec3 m_start_axis_point;
	DVec3 m_start_plane_point;
	Vec2 m_start_mouse_pos;
	float m_angle_accum;
	bool m_is_dragging;
	int m_active;
	Vec2 m_mouse_pos;
	Vec2 m_rel_accum;
	bool m_is_step;
	int m_count;
	EntityRef m_entities[MAX_GIZMOS];
	Vec3 m_offset;
};


Gizmo* Gizmo::create(WorldEditor& editor)
{
	return LUMIX_NEW(editor.getAllocator(), GizmoImpl)(editor);
}


void Gizmo::destroy(Gizmo& gizmo)
{
	LUMIX_DELETE(static_cast<GizmoImpl&>(gizmo).m_editor.getAllocator(), &gizmo);
}


} // namespace Lumix
