#pragma once


#include "lumix.h"
#include "core/vec3.h"
#include "universe/universe.h"
#include "renderer/model.h"
#include <bgfx/bgfx.h>


namespace Lumix
{


class Event;
struct Matrix;
class PipelineInstance;
class RenderScene;
class Universe;
class WorldEditor;


class LUMIX_EDITOR_API Gizmo
{
	public:
		enum class Flags : uint32_t
		{
			FIXED_STEP = 1
		};

		enum class Mode : uint32_t
		{
			ROTATE,
			TRANSLATE
		};

		enum class Axis: uint32_t
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

		enum class CoordSystem
		{
			LOCAL,
			WORLD
		};

	public:
		Gizmo(WorldEditor& editor);
		~Gizmo();

		void setCameraRay(const Vec3& origin, const Vec3& cursor_dir);
		void create();
		void destroy();
		void updateScale(ComponentIndex camera);
		void setUniverse(Universe* universe);
		void startTransform(ComponentIndex camera, int x, int y);
		void stopTransform();
		void setMode(Mode mode) { m_mode = mode; }
		Mode getMode() const { return m_mode; }
		void transform(ComponentIndex camera, int x, int y, int relx, int rely, int flags);
		void render(PipelineInstance& pipeline);
		bool castRay(const Vec3& origin, const Vec3& dir);
		void togglePivot();
		void toggleCoordSystem();

	private:
		void getMatrix(Matrix& mtx);
		void getEnityMatrix(Matrix& mtx, int selection_index);
		Vec3 getMousePlaneIntersection(ComponentIndex camera, int x, int y);
		void rotate(int relx, int rely, int flags);
		float computeRotateAngle(int relx, int rely, int flags);
		void renderTranslateGizmo(PipelineInstance& pipeline);
		void renderRotateGizmo(PipelineInstance& pipeline);
		void renderQuarterRing(PipelineInstance& pipeline, const Matrix& mtx, const Vec3& a, const Vec3& b, uint32_t color);

	private:
		WorldEditor& m_editor;
		RenderScene* m_scene;
		Universe* m_universe;
		Axis m_transform_axis;
		Vec3 m_transform_point;
		int m_relx_accum;
		int m_rely_accum;
		float m_scale;
		class Shader* m_shader;
		Pivot m_pivot;
		Mode m_mode;
		CoordSystem m_coord_system;
		bool m_is_transforming;
		Vec3 m_camera_dir;
		bgfx::VertexDecl m_vertex_decl;
};


} // !namespace Lumix
