#pragma once


#include "lux.h"
#include "vec3.h"


namespace Lux
{


LUX_CORE_API Vec3 getRayPlaneIntersecion(const Vec3& origin, const Vec3& dir, const Vec3& plane_point, const Vec3& normal);


}