#pragma once


#include "core/lux.h"
#include "core/matrix.h"
#include "core/vector.h"


namespace Lux
{
	class  Universe;
	struct Component;

	struct LUX_ENGINE_API Entity LUX_FINAL
	{
		typedef vector<Component> ComponentList;

		Entity() {}
		Entity(Universe* uni, int i) : index(i), universe(uni) {}

		Matrix getMatrix() const;
		void getMatrix(Matrix& mtx) const;
		void setMatrix(const Vec3& pos, const Quat& rot);
		void setMatrix(const Matrix& mtx);
		void setPosition(float x, float y, float z);
		void setPosition(const Vec3& v);
		const Vec3& getPosition() const;
		const Quat& getRotation() const;
		void setRotation(float x, float y, float z, float w);
		void setRotation(const Quat& rot);
		void translate(const Vec3& t);
		bool isValid() const { return index >= 0; }
		const Component& getComponent(uint32_t type);
		const ComponentList& getComponents() const;
		bool existsInUniverse() const;

		bool operator ==(const Entity& rhs) const;

		int index;
		Universe* universe;

		static const Entity INVALID;
	};
} // ~namepsace Lux