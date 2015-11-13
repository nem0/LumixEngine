#pragma once

#include "core/vec.h"


namespace Lumix
{
	struct Plane
	{
		Plane()
		{ }

		Plane(const Vec3& normal, float d)
			: normal(normal.x, normal.y, normal.z)
			, d(d)
		{ }

		explicit Plane(const Vec4& rhs)
			: normal(rhs.x, rhs.y, rhs.z)
			, d(rhs.w)
		{ }

		Plane(const Vec3& point, const Vec3& normal)
			: normal(normal.x, normal.y, normal.z)
			, d(-dotProduct(point, normal))
		{ }

		void set(const Vec3& normal, float d)
		{
			this->normal = normal;
			this->d = d;
		}

		void set(const Vec3& normal, const Vec3& point)
		{
			this->normal = normal;
			d = -dotProduct(point, normal);
		}

		void set(const Vec4& rhs)
		{
			normal.x = rhs.x;
			normal.y = rhs.y;
			normal.z = rhs.z;
			d = rhs.w;
		}

		Vec3 getNormal() const { return normal; }

		float getD() const { return d; }

		float distance(const Vec3& point) const	{ return dotProduct(point, normal) + d; }

		bool getIntersectionWithLine(const Vec3& line_point, const Vec3& line_vect, Vec3& out_intersection) const
		{
			float t2 = dotProduct(normal, line_vect);

			if (t2 == 0.f)
				return false;

			float t = -(dotProduct(normal, line_point) + d) / t2;
			out_intersection = line_point + (line_vect * t);
			return true;
		}

		Vec3 normal;
		float d;
	};
}
