#pragma once


#include "lumix.h"
#include "core/vec.h"
#include "universe/universe.h"
#include "renderer/model.h"
#include <bgfx/bgfx.h>


namespace Lumix
{


class Event;
struct Matrix;
class Pipeline;
class RenderScene;
class Universe;
class WorldEditor;


class LUMIX_EDITOR_API Gizmo
{
	public:
		enum class Mode : uint32
		{
			ROTATE,
			TRANSLATE,

			COUNT
		};

		enum class Axis: uint32
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
		void transform(ComponentIndex camera, int x, int y, int relx, int rely, bool use_step);
		void render(Pipeline& pipeline);
		bool isHit();
		void togglePivot();
		void toggleCoordSystem();
		int getStep() const { return m_step[int(m_mode)]; }
		void setStep(int step) { m_step[int(m_mode)] = step; }
		bool isAutosnapDown() const { return m_is_autosnap_down; }
		void setAutosnapDown(bool snap) { m_is_autosnap_down = snap; }

	private:
		void getMatrix(Matrix& mtx);
		void getEnityMatrix(Matrix& mtx, int selection_index);
		Vec3 getMousePlaneIntersection(ComponentIndex camera, int x, int y);
		void rotate(int relx, int rely, bool use_step);
		float computeRotateAngle(int relx, int rely, bool use_step);
		void renderTranslateGizmo(Pipeline& pipeline);
		void renderRotateGizmo(Pipeline& pipeline);
		void renderQuarterRing(Pipeline& pipeline, const Matrix& mtx, const Vec3& a, const Vec3& b, uint32 color);

	private:
		WorldEditor& m_editor;
		RenderScene* m_scene;
		Universe* m_universe;
		Axis m_transform_axis;
		Vec3 m_transform_point;
		int m_step[int(Mode::COUNT)];
		int m_relx_accum;
		int m_rely_accum;
		float m_scale;
		class Shader* m_shader;
		Pivot m_pivot;
		Mode m_mode;
		CoordSystem m_coord_system;
		bool m_is_transforming;
		bool m_is_autosnap_down;
		Vec3 m_camera_dir;
};


} // !namespace Lumix
