#pragma once

#include "simd.h"

namespace Lumix {

struct SOAVec3 {
	float4 x, y, z;
};

struct SOAQuat {
	float4 x, y, z, w;
};

struct SIMDLocalRigidTransform {
	SOAVec3 pos;
	SOAQuat rot;
};

struct SIMDDualQuat {
	SOAQuat r;
	SOAQuat d;
};

LUMIX_FORCE_INLINE void transposeStore(SOAQuat& quat, void* ptr) {
	u8* mem = (u8*)ptr;
	f4Transpose(quat.x, quat.y, quat.z, quat.w);
	f4Store(mem, quat.x);
	f4Store(mem + 16, quat.y);
	f4Store(mem + 32, quat.z);
	f4Store(mem + 48, quat.w);
}

LUMIX_FORCE_INLINE void loadTranspose(SOAQuat& quat, const void* ptr) {
	const u8* mem = (const u8*)ptr;
	quat.x = f4Load(mem);
	quat.y = f4Load(mem + 16);
	quat.z = f4Load(mem + 32);
	quat.w = f4Load(mem + 48);
	f4Transpose(quat.x, quat.y, quat.z, quat.w);
}

// 9 instructions
LUMIX_FORCE_INLINE SOAVec3 cross(SOAVec3 op1, SOAVec3 op2) {
	return SOAVec3(op1.y * op2.z - op1.z * op2.y, op1.z * op2.x - op1.x * op2.z, op1.x * op2.y - op1.y * op2.x);
}

LUMIX_FORCE_INLINE SOAVec3 operator *(SOAVec3 a, float4 b) {
	return {
		a.x * b,
		a.y * b,
		a.z * b
	};
}

LUMIX_FORCE_INLINE SOAVec3 operator *(SOAVec3 a, float b) {
	return {
		a.x * b,
		a.y * b,
		a.z * b
	};
}

LUMIX_FORCE_INLINE SOAVec3 operator +(SOAVec3 a, SOAVec3 b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

// 32 instructions
LUMIX_FORCE_INLINE SOAVec3 rotate(SOAQuat rot, SOAVec3 pos) {
	SOAVec3 qvec(rot.x, rot.y, rot.z);
	SOAVec3 uv = cross(qvec, pos); // 9 inst
	SOAVec3 uuv = cross(qvec, uv); // 9 inst
	uv = uv * (rot.w * 2.f); // 5 inst
	uuv = uuv * 2.0f; // 3 inst

	return pos + uv + uuv; // 6 inst
}

// 28 instructions
LUMIX_FORCE_INLINE SOAQuat operator *(SOAQuat a, SOAQuat b) {
	return {
		a.w * b.x + b.w * a.x + a.y * b.z - b.y * a.z,
		a.w * b.y + b.w * a.y + a.z * b.x - b.z * a.x,
		a.w * b.z + b.w * a.z + a.x * b.y - b.x * a.y,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z
	};
}

LUMIX_FORCE_INLINE SIMDLocalRigidTransform operator *(const SIMDLocalRigidTransform& a, const SIMDLocalRigidTransform& b) {
	return {rotate(a.rot, b.pos) + a.pos, a.rot * b.rot};	
}

LUMIX_FORCE_INLINE SIMDDualQuat toDualQuat(const SIMDLocalRigidTransform& t) {
	SIMDDualQuat res;
	res.r = t.rot;

	res.d = {
		(t.pos.x * t.rot.w + t.pos.y * t.rot.z - t.pos.z * t.rot.y) * 0.5f,
		(-t.pos.x * t.rot.z + t.pos.y * t.rot.w + t.pos.z * t.rot.x) * 0.5f,
		(t.pos.x * t.rot.y - t.pos.y * t.rot.x + t.pos.z * t.rot.w) * 0.5f,
		(t.pos.x * t.rot.x + t.pos.y * t.rot.y + t.pos.z * t.rot.z) * -0.5f
	};
	return res;		
}


LUMIX_FORCE_INLINE float4 simd_nlerp(float4 q1, float4 q2, float t) {
	Quat res;
	float inv = 1.0f - t;
	float4 q = q1 * q2;
	q = _mm_hadd_ps(q, q);
	q = _mm_hadd_ps(q, q);
	float d = f4GetX(q);
	if (d < 0) t = -t;
	q = q1 * inv + q2 * t;
	
	float4 qtmp = q * q;
	qtmp = _mm_hadd_ps(qtmp, qtmp);
	qtmp = _mm_hadd_ps(qtmp, qtmp);
	float l = 1 / f4GetX(f4Sqrt(qtmp));
	q = q * l;
	return q;
}

LUMIX_FORCE_INLINE Quat simd_nlerp(const Quat& a, const Quat& b, float t) {
	float4 q1 = f4LoadUnaligned(&a);
	float4 q2 = f4LoadUnaligned(&b);
	float4 q = simd_nlerp(q1, q2, t);
	Quat res;
	f4StoreUnaligned(&res, q);
	return res;
}

}