#include "math_utils.h"


namespace Lux
{


Vec3 getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal)
{
	float d = dotProduct(plane_point - origin, normal) / dotProduct(dir, normal);
	return dir * d + origin;
}


}