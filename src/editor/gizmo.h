#pragma once


#include "core/lumix.h"
#include "core/vec3.h"
#include "universe/universe.h"
#include "graphics/model.h"


namespace Lumix
{


class Event;
class IRenderDevice;
struct Matrix;
class Renderer;
class Universe;
class WorldEditor;


class LUMIX_ENGINE_API Gizmo
{
	public:
		struct Flags
		{
			enum Value
			{
				FIXED_STEP = 1
			};

			Flags() {}
			Flags(Value _value) : value(_value) {}
			Flags(int _value) : value((Value)_value) {}
		
			operator Value() const { return value; }
			operator int() const { return value; }

			Value value;
		};

		struct TransformOperation
		{
			enum Value
			{
				ROTATE,
				TRANSLATE
			};

			TransformOperation() {}
			TransformOperation(Value _value) : value(_value) {}

			operator Value() const { return value; }

			Value value;
		};

		struct TransformMode
		{
			enum Value
			{
				X,
				Y,
				Z,
				CAMERA_XZ
			};

			TransformMode() {}
			TransformMode(Value _value) : value(_value) {}

			operator Value() const { return value; }

			Value value;
		};

	public:
		Gizmo(WorldEditor& editor);
		~Gizmo();

		void create(Renderer& renderer);
		void destroy();
		void hide();
		void show();
		void updateScale(Component camera);
		void setEntity(Entity entity);
		void setUniverse(Universe* universe);
		void startTransform(Component camera, int x, int y, TransformMode mode);
		void transform(Component camera, TransformOperation operation, int x, int y, int relx, int rely, int flags);
		void render(Renderer& renderer, IRenderDevice& render_device);
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);

	private:
		void getMatrix(Matrix& mtx);
		Vec3 getMousePlaneIntersection(Component camera, int x, int y);

	private:
		WorldEditor& m_editor;
		Renderer* m_renderer;
		Entity m_selected_entity;
		Universe* m_universe;
		TransformMode m_transform_mode;
		Vec3 m_transform_point;
		int m_relx_accum;
		int m_rely_accum;
		class Model* m_model;
		float m_scale;
};


} // !namespace Lumix
