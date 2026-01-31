#pragma once

namespace black {

template <typename F>
struct Defer {
	F f;
	Defer(F&& f) : f(f) {}
	~Defer() { f(); }
};

struct DeferDummy {};

template <typename F>
Defer<F> operator+(DeferDummy, F&& f) {
	return Defer<F>(static_cast<F&&>(f));
}

#undef defer

#define DEFER_CONCAT_IMPL(x, y) x##y
#define DEFER_CONCAT(x, y) DEFER_CONCAT_IMPL(x, y)
#define defer auto DEFER_CONCAT(_defer_, __LINE__) = ::black.h::DeferDummy{} + [&]()

}