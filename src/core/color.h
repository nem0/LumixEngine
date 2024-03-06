#pragma once

#include "core.h"

namespace Lumix {
#pragma pack(1)
struct Color {
	Color() {}
	Color(u32 abgr) {
		r = u8(abgr & 0xff);
		g = u8((abgr >> 8) & 0xff);
		b = u8((abgr >> 16) & 0xff);
		a = u8((abgr >> 24) & 0xff);
	}

	Color(u8 r, u8 g, u8 b, u8 a)
		: r(r)
		, g(g)
		, b(b)
		, a(a) {}

	u32 abgr() const { return ((u32)a << 24) | ((u32)b << 16) | ((u32)g << 8) | (u32)r; }
	bool operator!=(const Color& rhs) { return abgr() != rhs.abgr(); }
	void operator*=(const Color& rhs) {
		r = u8((u32(rhs.r) * r) >> 8);
		g = u8((u32(rhs.g) * g) >> 8);
		b = u8((u32(rhs.b) * b) >> 8);
		a = u8((u32(rhs.a) * a) >> 8);
	}

	u8 r;
	u8 g;
	u8 b;
	u8 a;

	enum { RED = 0xff0000ff, GREEN = 0xff00ff00, BLUE = 0xffff0000, BLACK = 0xff000000, WHITE = 0xffFFffFF };
};
#pragma pack()
} // namespace Lumix