#pragma once


#include "Horde3D.h"
#include "core/vec3.h"
#include "universe/universe.h"


namespace Lux
{


struct Matrix;
class Universe;
class Event;
class Renderer;


class Gizmo
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

		void create(const char* base_path);
		void destroy();
		void hide();
		void show();
		void setMatrix(const Matrix& mtx);
		void getMatrix(Matrix& mtx);
		void updateScale(Renderer* renderer);
		void setEntity(Entity entity);
		void setUniverse(Universe* universe);
		void startTransform(int x, int y, TransformMode mode, Renderer* renderer);
		void transform(TransformOperation operation, int x, int y, int relx, int rely, Renderer* renderer, int flags);
		H3DNode getNode() const { return m_handle; }

	private:
		static void onEvent(void* data, Event& evt);
		Vec3 getMousePlaneIntersection(int x, int y, Renderer* renderer);

	private:
		H3DNode m_handle;
		Entity m_selected_entity;
		Universe* m_universe;
		TransformMode m_transform_mode;
		Vec3 m_transform_point;
		int m_relx_accum;
		int m_rely_accum;
};


} // !namespace Lux