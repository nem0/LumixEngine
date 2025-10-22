#pragma once

#include "core/atomic.h"
#include "engine/lumix.h"
#include "gpu/gpu.h"

namespace Lumix {

struct IAllocator;
struct Matrix;
struct Model;
struct Quat;
struct Vec3;

struct LUMIX_RENDERER_API Pose {
	explicit Pose(IAllocator& allocator);
	~Pose();

	void resize(int count);
	void computeAbsolute(Model& model);
	void computeRelative(Model& model);
	void blend(Pose& rhs, float weight);

	IAllocator& allocator;
	bool is_absolute;
	u32 count = 0;
	Vec3* positions = nullptr;
	Quat* rotations = nullptr;
	AtomicI32 frame = 0xffFFffFF;
	TransientSlice slice = {};
	
	private:
		Pose(const Pose&);
		void operator =(const Pose&);
};


} // namespace Lumix
