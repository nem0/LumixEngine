#pragma once


#include "engine/lumix.h"


namespace Lumix
{


class IAllocator;
struct Matrix;
class Model;
struct Quat;
struct Vec3;


struct LUMIX_RENDERER_API Pose
{
	explicit Pose(IAllocator& allocator);
	~Pose();

	void resize(int count);
	void setMatrices(Matrix* mtx) const;
	void computeAbsolute(Model& model);
	void computeRelative(Model& model);
	void blend(Pose& rhs, float weight);

	IAllocator& allocator;
	bool is_absolute;
	int32 count;
	Vec3* positions;
	Quat* rotations;
	
	private:
		Pose(const Pose&);
		void operator =(const Pose&);
};


} // ~namespace Lumix
