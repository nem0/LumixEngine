#pragma once


#include "core/lux.h"
#include "core/vec3.h"
#include "universe/universe.h"
#include "graphics/model.h"


namespace Lux
{


struct Matrix;
class Universe;
class Event;
class Renderer;


class LUX_ENGINE_API Gizmo
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
		Gizmo();
		~Gizmo();

		void create(const char* base_path, Renderer& renderer);
		void destroy();
		void hide();
		void show();
		void updateScale(Component camera);
		void setEntity(Entity entity);
		void setUniverse(Universe* universe);
		void startTransform(Component camera, int x, int y, TransformMode mode);
		void transform(Component camera, TransformOperation operation, int x, int y, int relx, int rely, int flags);
		void render(Renderer& renderer);
		RayCastModelHit castRay(const Vec3& origin, const Vec3& dir);

	private:
		void getMatrix(Matrix& mtx);
		Vec3 getMousePlaneIntersection(Component camera, int x, int y);

	private:
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


} // !namespace Lux