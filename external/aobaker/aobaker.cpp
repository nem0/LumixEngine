#include "aobaker.h"

#include <vector>
#include <utility>
#include <cmath>

#define PI 3.14159265358979323846264
#define RASTERIZE_MAX_DISTANCE 0.7

namespace aobaker {

	struct vec3 {
		float x;
		float y;
		float z;
		vec3::vec3() {};
		vec3::vec3(float x, float y, float z)
		{
			this->x = x;
			this->y = y;
			this->z = z;
		}
	};
	struct vec2 {
		float x;
		float y;
		vec2::vec2() {};
		vec2::vec2(float x, float y)
		{
			this->x = x;
			this->y = y;
		}
	};

	vec3 operator/(const vec3& v, const float& f)
	{
		return vec3(v.x / f, v.y / f, v.z / f);
	}
	vec3 operator*(const vec3& v, const float& f)
	{
		return vec3(v.x * f, v.y * f, v.z * f);
	}
	vec3 operator+(const vec3& v, const vec3& w)
	{
		return vec3(v.x + w.x, v.y + w.y, v.z + w.z);
	}
	vec3 operator-(const vec3& v, const vec3& w)
	{
		return vec3(v.x - w.x, v.y - w.y, v.z - w.z);
	}
	vec2 operator+(const vec2& v, const vec2& w)
	{
		return vec2(v.x + w.x, v.y + w.y);
	}
	vec2 operator-(const vec2& v, const vec2& w)
	{
		return vec2(v.x - w.x, v.y - w.y);
	}
	struct dvec3 {
		operator vec3() const { return vec3(x, y, z); }
		double x;
		double y;
		double z;
	};
	float RandomFloat()
	{
		return static_cast <float> (rand()) / static_cast <float> (RAND_MAX);
	}
	vec3 RandomUnitVec3()
	{
		vec3 rayDir;
		float phi = RandomFloat() * PI * 2.0f;
		float costheta = RandomFloat() * 2.0f - 1.0f;
		float theta = std::acos(costheta);
		rayDir.x = std::sin(theta) * std::cos(phi);
		rayDir.y = std::sin(theta) * std::sin(phi);
		rayDir.z = std::cos(theta);
		return rayDir;
	}
	vec3 MathCross(const vec3& a, const vec3& b)
	{
		vec3 product;
		product.x = (a.y * b.z) - (a.z * b.y);
		product.y = -((a.x * b.z) - (a.z * b.x));
		product.z = (a.x * b.y) - (a.y * b.x);
		return product;
	}
	float MathDot(const vec3& a, const vec3& b)
	{
		return a.x * b.x + a.y * b.y + a.z * b.z;
	}
	float MathMix(float x, float y, float a)
	{
		return x * (1 - a) + y * a;
	}
	vec3 MathNormalize(const vec3& v)
	{
		float mag = std::sqrt(MathDot(v, v));
		return vec3(v.x / mag, v.y / mag, v.z / mag);
	}
	float MathDistance(const vec3& a, const vec3& b)
	{
		vec3 c = b - a;
		return std::sqrt(MathDot(c, c));
	}
	bool MathPointInsideAABB(
		const vec3& point,
		const vec3& min,
		const vec3& max)
	{
		return point.x > min.x && point.y > min.y && point.z > min.z && point.x < max.x&& point.y < max.y&& point.z < max.z;
	}
	bool MathRayAABBIntersect(const vec3& origin, const vec3& dir, const vec3& min, const vec3& max, vec3* out)
	{
		vec3 dirfrac;

		dirfrac.x = 1.0f / (dir.x == 0 ? 0.00000001f : dir.x);
		dirfrac.y = 1.0f / (dir.y == 0 ? 0.00000001f : dir.y);
		dirfrac.z = 1.0f / (dir.z == 0 ? 0.00000001f : dir.z);

		float t1 = (min.x - origin.x) * dirfrac.x;
		float t2 = (max.x - origin.x) * dirfrac.x;
		float t3 = (min.y - origin.y) * dirfrac.y;
		float t4 = (max.y - origin.y) * dirfrac.y;
		float t5 = (min.z - origin.z) * dirfrac.z;
		float t6 = (max.z - origin.z) * dirfrac.z;

		float tmin = std::max(std::max(std::min(t1, t2), std::min(t3, t4)), std::min(t5, t6));
		float tmax = std::min(std::min(std::max(t1, t2), std::max(t3, t4)), std::max(t5, t6));

		if (tmax < 0 || tmin > tmax)
			return false;

		if (out != nullptr)
			*out = tmin < 0 ? origin : origin + dir * tmin;
		return true;
	}
	int MathClamp(int x, int min, int max)
	{
		return x < min ? min : (x > max ? max : x);
	}

	bool MathRayTriIntersect(const vec3& origin, const vec3& dir, const vec3& t0, const vec3& t1, const vec3& t2, float* out_t)
	{
		vec3 normal = MathCross(t1 - t0, t2 - t0);
		float q = MathDot(normal, dir);
		if (q == 0) return false;

		float d = -MathDot(normal, t0);
		float t = -(MathDot(normal, origin) + d) / q;
		if (t < 0) return false;

		vec3 hit_point = origin + dir * t;

		vec3 edge0 = t1 - t0;
		vec3 VP0 = hit_point - t0;
		if (MathDot(normal, MathCross(edge0, VP0)) < 0)
			return false;

		vec3 edge1 = t2 - t1;
		vec3 VP1 = hit_point - t1;
		if (MathDot(normal, MathCross(edge1, VP1)) < 0)
			return false;

		vec3 edge2 = t0 - t2;
		vec3 VP2 = hit_point - t2;
		if (MathDot(normal, MathCross(edge2, VP2)) < 0)
			return false;

		if (out_t) *out_t = t;
		return true;
	}

	float MathPlanePointDistance(
		vec3 planeNormal, vec3 planePoint,
		vec3 point)
	{
		return MathDot(MathNormalize(planeNormal), point - planePoint);
	}

	float MathLineSegmentPointDistance2D(const vec2& p, const vec2& l0, const vec2& l1)
	{
		float l2 = std::pow(l0.x - l1.x, 2.0f) + std::pow(l0.y - l1.y, 2.0f);
		if (l2 == 0.0f)
			return std::pow(p.x - l0.x, 2.0f) + std::pow(p.y - l0.y, 2.0f);
		float t = ((p.x - l0.x) * (l1.x - l0.x) + (p.y - l0.y) * (l1.y - l0.y)) / l2;
		t = std::max(0.0f, std::min(1.0f, t));
		vec2 o = { l0.x + t * (l1.x - l0.x), l0.y + t * (l1.y - l0.y) };
		return std::sqrt(std::pow(p.x - o.x, 2.0f) + std::pow(p.y - o.y, 2.0f));
	}

	bool MathTriPointIntersect2D(const vec2& p, const vec2& t0, const vec2& t1, const vec2& t2)
	{
		float d1, d2, d3;
		bool has_neg, has_pos;

		d1 = (p.x - t1.x) * (t0.y - t1.y) - (t0.x - t1.x) * (p.y - t1.y);
		d2 = (p.x - t2.x) * (t1.y - t2.y) - (t1.x - t2.x) * (p.y - t2.y);
		d3 = (p.x - t0.x) * (t2.y - t0.y) - (t2.x - t0.x) * (p.y - t0.y);

		has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
		has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);

		return !(has_neg && has_pos);
	}


	float MathTriPointDistance2D(const vec2& p, const vec2& t0, const vec2& t1, const vec2& t2)
	{
		if (MathTriPointIntersect2D(p, t0, t1, t2))
			return 0.0f;

		float disToSideA = MathLineSegmentPointDistance2D(p, t0, t1);
		float disToSideB = MathLineSegmentPointDistance2D(p, t1, t2);
		float disToSideC = MathLineSegmentPointDistance2D(p, t2, t0);

		return std::min(std::min(disToSideA, disToSideB), disToSideC);
	}

	struct voxelModel {
		vec3 minPos;
		float voxelSize;
		std::vector<bool> mat;
		unsigned int voxelCount[3];
		voxelModel::voxelModel(const float* firstVertexPosition, int vertexCount, unsigned int stride, const unsigned int* indices, int indexCount, float voxelSize)
		{
			this->voxelSize = voxelSize;

			vec3 minP = *((vec3*)firstVertexPosition);
			vec3 maxP = minP;

			for (int i = 1; i < vertexCount; i++)
			{
				vec3 currentVtxPos = *((vec3*)(((char*)firstVertexPosition) + stride * i));
				minP.x = std::min(minP.x, currentVtxPos.x);
				minP.y = std::min(minP.y, currentVtxPos.y);
				minP.z = std::min(minP.z, currentVtxPos.z);
				maxP.x = std::max(maxP.x, currentVtxPos.x);
				maxP.y = std::max(maxP.y, currentVtxPos.y);
				maxP.z = std::max(maxP.z, currentVtxPos.z);
			}

			minPos = minP;

			voxelCount[0] = (unsigned int)std::ceil((maxP.x - minP.x) / voxelSize);
			voxelCount[1] = (unsigned int)std::ceil((maxP.y - minP.y) / voxelSize);
			voxelCount[2] = (unsigned int)std::ceil((maxP.z - minP.z) / voxelSize);

			// allocate matrix
			mat.resize(voxelCount[0] * voxelCount[1] * voxelCount[2]);

			// rasterize
			for (int indexI = 0; indexI < indexCount; indexI += 3)
			{
				unsigned int indexA = indices[indexI + 0];
				unsigned int indexB = indices[indexI + 1];
				unsigned int indexC = indices[indexI + 2];

				vec3 posA = *((vec3*)(((char*)firstVertexPosition) + stride * indexA));
				vec3 posB = *((vec3*)(((char*)firstVertexPosition) + stride * indexB));
				vec3 posC = *((vec3*)(((char*)firstVertexPosition) + stride * indexC));

				vec3 triNormal = MathCross(posB - posA, posC - posA);

				vec3 trianglebbmin = {
					std::min(std::min(posA.x, posB.x), posC.x),
					std::min(std::min(posA.y, posB.y), posC.y),
					std::min(std::min(posA.z, posB.z), posC.z)
				};
				vec3 trianglebbmax = {
					std::max(std::max(posA.x, posB.x), posC.x),
					std::max(std::max(posA.y, posB.y), posC.y),
					std::max(std::max(posA.z, posB.z), posC.z)
				};

				unsigned int minVoxelCoords[3];
				minVoxelCoords[0] = MathClamp((int)((trianglebbmin.x - minPos.x) / voxelSize), 0, (int)(voxelCount[0] - 1));
				minVoxelCoords[1] = MathClamp((int)((trianglebbmin.y - minPos.y) / voxelSize), 0, (int)(voxelCount[1] - 1));
				minVoxelCoords[2] = MathClamp((int)((trianglebbmin.z - minPos.z) / voxelSize), 0, (int)(voxelCount[2] - 1));

				unsigned int maxVoxelCoords[3];
				maxVoxelCoords[0] = MathClamp((int)((trianglebbmax.x - minPos.x) / voxelSize), 0, (int)(voxelCount[0] - 1));
				maxVoxelCoords[1] = MathClamp((int)((trianglebbmax.y - minPos.y) / voxelSize), 0, (int)(voxelCount[1] - 1));
				maxVoxelCoords[2] = MathClamp((int)((trianglebbmax.z - minPos.z) / voxelSize), 0, (int)(voxelCount[2] - 1));

				std::vector<bool> xyMat;
				std::vector<bool> xzMat;
				std::vector<bool> zyMat;

				unsigned int xCount = maxVoxelCoords[0] - minVoxelCoords[0] + 1;
				unsigned int yCount = maxVoxelCoords[1] - minVoxelCoords[1] + 1;
				unsigned int zCount = maxVoxelCoords[2] - minVoxelCoords[2] + 1;
				xyMat.resize(xCount * yCount);
				xzMat.resize(xCount * zCount);
				zyMat.resize(zCount * yCount);

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						vec2 voxelCenter = vec2(minPos.x, minPos.y) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.x, posA.y);
						vec2 tri1 = vec2(posB.x, posB.y);
						vec2 tri2 = vec2(posC.x, posC.y);
						xyMat[(i - minVoxelCoords[0]) * yCount + (j - minVoxelCoords[1])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[2]; j <= maxVoxelCoords[2]; j++)
					{
						vec2 voxelCenter = vec2(minPos.x, minPos.z) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.x, posA.z);
						vec2 tri1 = vec2(posB.x, posB.z);
						vec2 tri2 = vec2(posC.x, posC.z);
						xzMat[(i - minVoxelCoords[0]) * zCount + (j - minVoxelCoords[2])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[2]; i <= maxVoxelCoords[2]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						vec2 voxelCenter = vec2(minPos.z, minPos.y) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.z, posA.y);
						vec2 tri1 = vec2(posB.z, posB.y);
						vec2 tri2 = vec2(posC.z, posC.y);
						zyMat[(i - minVoxelCoords[2]) * yCount + (j - minVoxelCoords[1])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						for (int k = minVoxelCoords[2]; k <= maxVoxelCoords[2]; k++)
						{
							vec3 voxelCenter =
								vec3(minPos.x, minPos.y, minPos.z) +
								vec3(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f, k * voxelSize + voxelSize / 2.0f);

							bool voxelValueForCurrentTri =
								xyMat[(i - minVoxelCoords[0]) * yCount + (j - minVoxelCoords[1])] &&
								xzMat[(i - minVoxelCoords[0]) * zCount + (k - minVoxelCoords[2])] &&
								zyMat[(k - minVoxelCoords[2]) * yCount + (j - minVoxelCoords[1])] &&
								std::abs(MathPlanePointDistance(triNormal, posA, voxelCenter)) < voxelSize * RASTERIZE_MAX_DISTANCE;

							mat[i * voxelCount[1] * voxelCount[2] + j * voxelCount[2] + k] = mat[i * voxelCount[1] * voxelCount[2] + j * voxelCount[2] + k] || voxelValueForCurrentTri;
						}
					}
				}
			}
		}
		voxelModel::voxelModel(const double* firstVertexPosition, int vertexCount, unsigned int stride, const unsigned int* indices, int indexCount, float voxelSize)
		{
			this->voxelSize = voxelSize;

			vec3 minP = *((dvec3*)firstVertexPosition);
			vec3 maxP = minP;

			for (int i = 1; i < vertexCount; i++)
			{
				vec3 currentVtxPos = *((dvec3*)(((char*)firstVertexPosition) + stride * i));
				minP.x = std::min(minP.x, currentVtxPos.x);
				minP.y = std::min(minP.y, currentVtxPos.y);
				minP.z = std::min(minP.z, currentVtxPos.z);
				maxP.x = std::max(maxP.x, currentVtxPos.x);
				maxP.y = std::max(maxP.y, currentVtxPos.y);
				maxP.z = std::max(maxP.z, currentVtxPos.z);
			}

			minPos = minP;

			voxelCount[0] = (unsigned int)std::ceil((maxP.x - minP.x) / voxelSize);
			voxelCount[1] = (unsigned int)std::ceil((maxP.y - minP.y) / voxelSize);
			voxelCount[2] = (unsigned int)std::ceil((maxP.z - minP.z) / voxelSize);

			// allocate matrix
			mat.resize(voxelCount[0] * voxelCount[1] * voxelCount[2]);

			// rasterize
			for (int indexI = 0; indexI < indexCount; indexI += 3)
			{
				unsigned int indexA = indices[indexI + 0];
				unsigned int indexB = indices[indexI + 1];
				unsigned int indexC = indices[indexI + 2];

				vec3 posA = *((dvec3*)(((char*)firstVertexPosition) + stride * indexA));
				vec3 posB = *((dvec3*)(((char*)firstVertexPosition) + stride * indexB));
				vec3 posC = *((dvec3*)(((char*)firstVertexPosition) + stride * indexC));

				vec3 triNormal = MathCross(posB - posA, posC - posA);

				vec3 trianglebbmin = {
					std::min(std::min(posA.x, posB.x), posC.x),
					std::min(std::min(posA.y, posB.y), posC.y),
					std::min(std::min(posA.z, posB.z), posC.z)
				};
				vec3 trianglebbmax = {
					std::max(std::max(posA.x, posB.x), posC.x),
					std::max(std::max(posA.y, posB.y), posC.y),
					std::max(std::max(posA.z, posB.z), posC.z)
				};

				unsigned int minVoxelCoords[3];
				minVoxelCoords[0] = MathClamp((int)((trianglebbmin.x - minPos.x) / voxelSize), 0, (int)(voxelCount[0] - 1));
				minVoxelCoords[1] = MathClamp((int)((trianglebbmin.y - minPos.y) / voxelSize), 0, (int)(voxelCount[1] - 1));
				minVoxelCoords[2] = MathClamp((int)((trianglebbmin.z - minPos.z) / voxelSize), 0, (int)(voxelCount[2] - 1));

				unsigned int maxVoxelCoords[3];
				maxVoxelCoords[0] = MathClamp((int)((trianglebbmax.x - minPos.x) / voxelSize), 0, (int)(voxelCount[0] - 1));
				maxVoxelCoords[1] = MathClamp((int)((trianglebbmax.y - minPos.y) / voxelSize), 0, (int)(voxelCount[1] - 1));
				maxVoxelCoords[2] = MathClamp((int)((trianglebbmax.z - minPos.z) / voxelSize), 0, (int)(voxelCount[2] - 1));

				std::vector<bool> xyMat;
				std::vector<bool> xzMat;
				std::vector<bool> zyMat;

				unsigned int xCount = maxVoxelCoords[0] - minVoxelCoords[0] + 1;
				unsigned int yCount = maxVoxelCoords[1] - minVoxelCoords[1] + 1;
				unsigned int zCount = maxVoxelCoords[2] - minVoxelCoords[2] + 1;
				xyMat.resize(xCount * yCount);
				xzMat.resize(xCount * zCount);
				zyMat.resize(zCount * yCount);

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						vec2 voxelCenter = vec2(minPos.x, minPos.y) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.x, posA.y);
						vec2 tri1 = vec2(posB.x, posB.y);
						vec2 tri2 = vec2(posC.x, posC.y);
						xyMat[(i - minVoxelCoords[0]) * yCount + (j - minVoxelCoords[1])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[2]; j <= maxVoxelCoords[2]; j++)
					{
						vec2 voxelCenter = vec2(minPos.x, minPos.z) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.x, posA.z);
						vec2 tri1 = vec2(posB.x, posB.z);
						vec2 tri2 = vec2(posC.x, posC.z);
						xzMat[(i - minVoxelCoords[0]) * zCount + (j - minVoxelCoords[2])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[2]; i <= maxVoxelCoords[2]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						vec2 voxelCenter = vec2(minPos.z, minPos.y) + vec2(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f);
						vec2 tri0 = vec2(posA.z, posA.y);
						vec2 tri1 = vec2(posB.z, posB.y);
						vec2 tri2 = vec2(posC.z, posC.y);
						zyMat[(i - minVoxelCoords[2]) * yCount + (j - minVoxelCoords[1])] = MathTriPointDistance2D(voxelCenter, tri0, tri1, tri2) < voxelSize * RASTERIZE_MAX_DISTANCE;
					}
				}

				for (int i = minVoxelCoords[0]; i <= maxVoxelCoords[0]; i++)
				{
					for (int j = minVoxelCoords[1]; j <= maxVoxelCoords[1]; j++)
					{
						for (int k = minVoxelCoords[2]; k <= maxVoxelCoords[2]; k++)
						{
							vec3 voxelCenter =
								vec3(minPos.x, minPos.y, minPos.z) +
								vec3(i * voxelSize + voxelSize / 2.0f, j * voxelSize + voxelSize / 2.0f, k * voxelSize + voxelSize / 2.0f);

							bool voxelValueForCurrentTri =
								xyMat[(i - minVoxelCoords[0]) * yCount + (j - minVoxelCoords[1])] &&
								xzMat[(i - minVoxelCoords[0]) * zCount + (k - minVoxelCoords[2])] &&
								zyMat[(k - minVoxelCoords[2]) * yCount + (j - minVoxelCoords[1])] &&
								std::abs(MathPlanePointDistance(triNormal, posA, voxelCenter)) < voxelSize * RASTERIZE_MAX_DISTANCE;

							mat[i * voxelCount[1] * voxelCount[2] + j * voxelCount[2] + k] = mat[i * voxelCount[1] * voxelCount[2] + j * voxelCount[2] + k] || voxelValueForCurrentTri;
						}
					}
				}
			}
		}
		vec3 getAABBMin() const
		{
			return minPos;
		}
		vec3 getAABBMax() const
		{
			return minPos + vec3(voxelCount[0] * voxelSize, voxelCount[1] * voxelSize, voxelCount[2] * voxelSize);
		}
		vec3 getVoxelCenterLocation(const unsigned int* coords) const
		{
			return vec3(
				minPos.x + (voxelSize * (float)coords[0]) + voxelSize / 2.0f,
				minPos.y + (voxelSize * (float)coords[1]) + voxelSize / 2.0f,
				minPos.z + (voxelSize * (float)coords[2]) + voxelSize / 2.0f);
		}
		// https://www.researchgate.net/publication/2611491_A_Fast_Voxel_Traversal_Algorithm_for_Ray_Tracing
		bool castRay(const vec3& origin, const vec3& direction, bool avoidEarlyCollision, float* out_t) const
		{
			vec3 dir = MathNormalize(direction);
			if (dir.x == 0.0f) dir.x = 0.00000001f;
			if (dir.y == 0.0f) dir.y = 0.00000001f;
			if (dir.z == 0.0f) dir.z = 0.00000001f;

			int step[3] = {
				dir.x > 0.0f ? 1 : -1,
				dir.y > 0.0f ? 1 : -1,
				dir.z > 0.0f ? 1 : -1
			};

			vec3 bbmin = getAABBMin();
			vec3 bbmax = getAABBMax();

			unsigned int currentVoxel[3];
			if (!MathPointInsideAABB(origin, bbmin, bbmax))
			{
				vec3 point;
				// move origin to bounding box
				if (!MathRayAABBIntersect(origin, dir, bbmin, bbmax, &point))
					return false;

				currentVoxel[0] = (point.x - bbmin.x) / voxelSize;
				currentVoxel[1] = (point.y - bbmin.y) / voxelSize;
				currentVoxel[2] = (point.z - bbmin.z) / voxelSize;

				if (currentVoxel[0] < 0) currentVoxel[0] = 0;
				if (currentVoxel[0] > voxelCount[0] - 1) currentVoxel[0] = voxelCount[0] - 1;
				if (currentVoxel[1] < 0) currentVoxel[1] = 0;
				if (currentVoxel[1] > voxelCount[1] - 1) currentVoxel[1] = voxelCount[1] - 1;
				if (currentVoxel[2] < 0) currentVoxel[2] = 0;
				if (currentVoxel[2] > voxelCount[2] - 1) currentVoxel[2] = voxelCount[2] - 1;
			}
			else
			{
				currentVoxel[0] = (origin.x - bbmin.x) / voxelSize;
				currentVoxel[1] = (origin.y - bbmin.y) / voxelSize;
				currentVoxel[2] = (origin.z - bbmin.z) / voxelSize;
			}

			vec3 currentVoxelCenter = getVoxelCenterLocation(currentVoxel);
			vec3 nextBoundaries = {
				currentVoxelCenter.x + (step[0] * voxelSize / 2.0f),
				currentVoxelCenter.y + (step[1] * voxelSize / 2.0f),
				currentVoxelCenter.z + (step[2] * voxelSize / 2.0f)
			};

			vec3 tMax = {
				(nextBoundaries.x - origin.x) / dir.x,
				(nextBoundaries.y - origin.y) / dir.y,
				(nextBoundaries.z - origin.z) / dir.z,
			};

			vec3 tDelta{
				voxelSize / dir.x,
				voxelSize / dir.y,
				voxelSize / dir.z
			};

			bool inAir = !avoidEarlyCollision;
			while (true)
			{
				if (tMax.x < tMax.y)
				{
					if (tMax.x < tMax.z)
					{
						currentVoxel[0] = currentVoxel[0] + step[0];
						if (currentVoxel[0] > voxelCount[0] - 1) return false;
						tMax.x = tMax.x + std::abs(tDelta.x);
					}
					else
					{
						currentVoxel[2] = currentVoxel[2] + step[2];
						if (currentVoxel[2] > voxelCount[2] - 1) return false;
						tMax.z = tMax.z + std::abs(tDelta.z);
					}
				}
				else
				{
					if (tMax.y < tMax.z)
					{
						currentVoxel[1] = currentVoxel[1] + step[1];
						if (currentVoxel[1] > voxelCount[1] - 1) return false;
						tMax.y = tMax.y + std::abs(tDelta.y);
					}
					else
					{
						currentVoxel[2] = currentVoxel[2] + step[2];
						if (currentVoxel[2] > voxelCount[2] - 1) return false;
						tMax.z = tMax.z + std::abs(tDelta.z);
					}
				}

				if (!mat[currentVoxel[0] * voxelCount[1] * voxelCount[2] + currentVoxel[1] * voxelCount[2] + currentVoxel[2]] && !inAir)
					inAir = true;

				if (mat[currentVoxel[0] * voxelCount[1] * voxelCount[2] + currentVoxel[1] * voxelCount[2] + currentVoxel[2]] && inAir)
				{
					if (out_t != nullptr)
						*out_t = MathDistance(getVoxelCenterLocation(currentVoxel), origin);
					return true;
				}
			}
		}
	};

	float ComputeOcclusion(const std::vector<std::pair<bool, float>>& rayResults, float maxDistance, float falloff)
	{
		float brightness = 1.0f;
		int hit_count = 0;
		for (const auto& r : rayResults)
		{
			if (r.first) // did hit
			{
				float normalizedDistance = r.second / maxDistance;
				float occlusion = 1.0f - std::pow(normalizedDistance, falloff);
				brightness -= occlusion / (float)rayResults.size();
			}
		}

		brightness = std::min(1.0f, brightness * std::sqrt(2.0f));
		return brightness;
	}
}

void aobaker::BakeAoToVertices(
	const float* firstVertexPosition, float* firstAoTarget, int vertexCount, unsigned int vertexStride, unsigned int targetStride,
	const unsigned int* indices, int indexCount,
	const config& conf)
{
	if (conf.voxelize)
	{
		voxelModel voxelized(firstVertexPosition, vertexCount, vertexStride, indices, indexCount, conf.voxelSize);

		#pragma omp parallel for
		for (int q = 0; q < vertexCount; q++)
		{
			vec3 vertexPos = *((vec3*)(((char*)firstVertexPosition) + vertexStride * q));
			float* aoTarget = (float*)(((char*)firstAoTarget) + targetStride * q);

			std::vector<std::pair<bool, float>> rayResults;
			for (int i = 0; i < conf.rayCount; i++)
			{
				vec3 rayDir = RandomUnitVec3();
				if (conf.onlyCastRaysUpwards && rayDir.y < 0.0f)
					rayDir.y = -rayDir.y;

				float distance;
				bool didHit = voxelized.castRay(vertexPos + (rayDir * conf.rayOriginOffset), rayDir, true, &distance);
				if (distance > conf.rayDistance)
					distance = conf.rayDistance;
				rayResults.push_back({ didHit, distance });
			}
			*aoTarget = ComputeOcclusion(rayResults, conf.rayDistance, conf.falloff);
		}
	}
	else
	{
		#pragma omp parallel for
		for (int q = 0; q < vertexCount; q++)
		{
			vec3 vertexPos = *((vec3*)(((char*)firstVertexPosition) + vertexStride * q));
			float* aoTarget = (float*)(((char*)firstAoTarget) + targetStride * q);

			std::vector<std::pair<bool, float>> rayResults;
			for (int i = 0; i < conf.rayCount; i++)
			{
				vec3 rayDir = RandomUnitVec3();
				if (conf.onlyCastRaysUpwards && rayDir.y < 0.0f)
					rayDir.y = -rayDir.y;

				bool didHit = false;
				float distance;
				for (int j = 0; j < indexCount && !didHit; j += 3) // for each face, intersect
				{
					if (indices[j + 0] == q || indices[j + 1] == q || indices[j + 2] == q)
						continue; // current vertex belongs to this face

					didHit = MathRayTriIntersect(vertexPos + (rayDir * conf.rayOriginOffset), rayDir,
						*((vec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 0])),
						*((vec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 1])),
						*((vec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 2])), &distance);
				}
				if (distance > conf.rayDistance)
					distance = conf.rayDistance;
				rayResults.push_back({ didHit, distance });
			}
			*aoTarget = ComputeOcclusion(rayResults, conf.rayDistance, conf.falloff);
		}
	}

	for (int pass = 0; pass < conf.denoisePasses; pass++)
	{
		for (int i = 0; i < indexCount; i += 3)
		{
			float average =
				(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])) +
					*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])) +
					*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2]))) / 3.0f;

			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])), average, conf.denoiseWeight);
			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])), average, conf.denoiseWeight);
			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2])), average, conf.denoiseWeight);
		}
	}
}

void aobaker::BakeAoToVertices(
	const double* firstVertexPosition, float* firstAoTarget, int vertexCount, unsigned int vertexStride, unsigned int targetStride,
	const unsigned int* indices, int indexCount,
	const config& conf)
{
	if (conf.voxelize)
	{
		voxelModel voxelized(firstVertexPosition, vertexCount, vertexStride, indices, indexCount, conf.voxelSize);

		#pragma omp parallel for
		for (int q = 0; q < vertexCount; q++)
		{
			vec3 vertexPos = *((dvec3*)(((char*)firstVertexPosition) + vertexStride * q));
			float* aoTarget = (float*)(((char*)firstAoTarget) + targetStride * q);

			std::vector<std::pair<bool, float>> rayResults;
			for (int i = 0; i < conf.rayCount; i++)
			{
				vec3 rayDir = RandomUnitVec3();
				if (conf.onlyCastRaysUpwards && rayDir.y < 0.0f)
					rayDir.y = -rayDir.y;

				float distance;
				bool didHit = voxelized.castRay(vertexPos + (rayDir * conf.rayOriginOffset), rayDir, true, &distance);
				if (distance > conf.rayDistance)
					distance = conf.rayDistance;
				rayResults.push_back({ didHit, distance });
			}
			*aoTarget = ComputeOcclusion(rayResults, conf.rayDistance, conf.falloff);
		}
	}
	else
	{
		#pragma omp parallel for
		for (int q = 0; q < vertexCount; q++)
		{
			vec3 vertexPos = *((dvec3*)(((char*)firstVertexPosition) + vertexStride * q));
			float* aoTarget = (float*)(((char*)firstAoTarget) + targetStride * q);

			std::vector<std::pair<bool, float>> rayResults;
			for (int i = 0; i < conf.rayCount; i++)
			{
				vec3 rayDir = RandomUnitVec3();
				if (conf.onlyCastRaysUpwards && rayDir.y < 0.0f)
					rayDir.y = -rayDir.y;

				bool didHit = false;
				float distance;
				for (int j = 0; j < indexCount && !didHit; j += 3) // for each face, intersect
				{
					if (indices[j + 0] == q || indices[j + 1] == q || indices[j + 2] == q)
						continue; // current vertex belongs to this face

					didHit = MathRayTriIntersect(vertexPos + (rayDir * conf.rayOriginOffset), rayDir,
						*((dvec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 0])),
						*((dvec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 1])),
						*((dvec3*)(((char*)firstVertexPosition) + vertexStride * indices[j + 2])), &distance);
				}
				if (distance > conf.rayDistance)
					distance = conf.rayDistance;
				rayResults.push_back({ didHit, distance });
			}
			*aoTarget = ComputeOcclusion(rayResults, conf.rayDistance, conf.falloff);
		}
	}

	for (int pass = 0; pass < conf.denoisePasses; pass++)
	{
		for (int i = 0; i < indexCount; i += 3)
		{
			float average =
				(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])) +
					*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])) +
					*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2]))) / 3.0f;

			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 0])), average, conf.denoiseWeight);
			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 1])), average, conf.denoiseWeight);
			*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2])) = MathMix(*((float*)(((char*)firstAoTarget) + targetStride * indices[i + 2])), average, conf.denoiseWeight);
		}
	}
}