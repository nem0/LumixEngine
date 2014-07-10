#pragma once

#include "core/vec3.h"
#include "core/vec4.h"

namespace Lux
{
	struct Plane
	{
		inline Plane()
		{ }

		inline Plane(const Vec3& normal, float d)
			: normal(normal.x, normal.y, normal.z)
			, d(d)
		{ }

		inline Plane(const Vec4& rhs)
			: normal(rhs.x, rhs.y, rhs.z)
			, d(rhs.w)
		{ }

		inline Plane(const Vec3& point, const Vec3& normal)
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

			if (t2 == 0)
				return false;

			float t = -(dotProduct(normal, line_point) + d) / t2;
			out_intersection = line_point + (line_vect * t);
			return true;
		}

		bool getIntersectionWithPlane(const Plane& other, Vec3& out_line_point, Vec3& out_line_vect) const
		{
			const float fn00 = normal.length();
			const float fn01 = dotProduct(normal, other.normal);
			const float fn11 = other.normal.length();
			const float det = fn00 * fn11 - fn01 * fn01;

			if (fabs(det) < 0.00000001)
				return false;

			const float invdet = 1.0f / det;
			const float fc0 = (fn11 * -d + fn01 * other.d) * invdet;
			const float fc1 = (fn00 * -other.d + fn01 * d) * invdet;

			out_line_vect = crossProduct(normal, other.normal);
			out_line_point = normal * (float)fc0 + other.normal * (float)fc1;
			return true;
		}

		bool getIntersectionWithPlanes(const Plane& p1,	const Plane& p2, Vec3& out_point) const
		{
			Vec3 line_point, line_vect;
			if (getIntersectionWithPlane(p1, line_point, line_vect))
				return p2.getIntersectionWithLine(line_point, line_vect, out_point);

			return false;
		}

		Vec3 normal;
		float d;
	};
}
