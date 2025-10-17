#pragma once

#include "simd.h"

namespace Lumix {

struct SIMDVec3 {
    float4 x, y, z;
};

struct SIMDQuat {
    float4 x, y, z, w;
};

struct SIMDLocalRigidTransform {
    SIMDVec3 pos;
    SIMDQuat rot;
};

struct SIMDDualQuat {
    SIMDQuat r;
    SIMDQuat d;
};

LUMIX_FORCE_INLINE SIMDVec3 cross(SIMDVec3 op1, SIMDVec3 op2) {
    return SIMDVec3(op1.y * op2.z - op1.z * op2.y, op1.z * op2.x - op1.x * op2.z, op1.x * op2.y - op1.y * op2.x);
}

LUMIX_FORCE_INLINE SIMDVec3 operator *(SIMDVec3 a, float4 b) {
    return {
        a.x * b,
        a.y * b,
        a.z * b
    };
}

LUMIX_FORCE_INLINE SIMDVec3 operator *(SIMDVec3 a, float b) {
    return {
        a.x * b,
        a.y * b,
        a.z * b
    };
}

LUMIX_FORCE_INLINE SIMDVec3 operator +(SIMDVec3 a, SIMDVec3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

LUMIX_FORCE_INLINE SIMDVec3 rotate(SIMDQuat rot, SIMDVec3 pos) {
    SIMDVec3 qvec(rot.x, rot.y, rot.z);
    SIMDVec3 uv = cross(qvec, pos);
    SIMDVec3 uuv = cross(qvec, uv);
    uv = uv * (rot.w * 2.f);
    uuv = uuv * 2.0f;

    return pos + uv + uuv;
}

LUMIX_FORCE_INLINE SIMDQuat operator *(SIMDQuat a, SIMDQuat b) {
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

}