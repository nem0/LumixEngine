#pragma once

#include "core/math.h"
#include "core/metaprogramming.h"
#include "lumscript/capi.h"

namespace Lumix::LumScript {

struct LumScriptEntity;

template <typename T> struct StackSlots {
	static constexpr int value = 1;
};

template <typename T> constexpr int stackSlots() {
	return StackSlots<RemoveCVR<T>>::value;
}

template <typename T> struct IsPointer { static constexpr bool Value = false; };
template <typename T> struct IsPointer<T*> { static constexpr bool Value = true; };

template <int I, typename... Args> struct ArgTypeAt;
template <typename T, typename... Args> struct ArgTypeAt<0, T, Args...> { using Type = T; };
template <int I, typename T, typename... Args> struct ArgTypeAt<I, T, Args...> { using Type = typename ArgTypeAt<I - 1, Args...>::Type; };

template <typename T> T checkArg(ls_runtime* runtime, int index) {
	static_assert(IsPointer<T>::Value);
	return (T)ls_to_ptr(runtime, index);
}

template <> inline bool checkArg<bool>(ls_runtime* runtime, int index) { return ls_to_bool(runtime, index); }
template <> inline u32 checkArg<u32>(ls_runtime* runtime, int index) { return ls_to_u32(runtime, index); }
template <> inline i32 checkArg<i32>(ls_runtime* runtime, int index) { return ls_to_i32(runtime, index); }
template <> inline float checkArg<float>(ls_runtime* runtime, int index) { return ls_to_f32(runtime, index); }
template <> inline double checkArg<double>(ls_runtime* runtime, int index) { return ls_to_f64(runtime, index); }
template <> inline ls_string_view checkArg<ls_string_view>(ls_runtime* runtime, int index) { return ls_to_string(runtime, index); }

template <typename T> void push(ls_runtime* runtime, T* value) {
	ls_push_ptr(runtime, (void*)value);
}

template <typename T> void push(ls_runtime* runtime, const T* value) {
	ls_push_ptr(runtime, (void*)value);
}

inline void push(ls_runtime* runtime, bool value) { ls_push_bool(runtime, value); }
inline void push(ls_runtime* runtime, u32 value) { ls_push_u32(runtime, value); }
inline void push(ls_runtime* runtime, i32 value) { ls_push_i32(runtime, value); }
inline void push(ls_runtime* runtime, float value) { ls_push_f32(runtime, value); }
inline void push(ls_runtime* runtime, double value) { ls_push_f64(runtime, value); }
inline void push(ls_runtime* runtime, ls_string_view value) { ls_push_string(runtime, value); }
inline void push(ls_runtime* runtime, const Vec3& value) {
	ls_push_f32(runtime, value.x);
	ls_push_f32(runtime, value.y);
	ls_push_f32(runtime, value.z);
}
inline void push(ls_runtime* runtime, const DVec3& value) {
	ls_push_f64(runtime, value.x);
	ls_push_f64(runtime, value.y);
	ls_push_f64(runtime, value.z);
}
inline void push(ls_runtime* runtime, const Quat& value) {
	ls_push_f32(runtime, value.x);
	ls_push_f32(runtime, value.y);
	ls_push_f32(runtime, value.z);
	ls_push_f32(runtime, value.w);
}
void push(ls_runtime* runtime, const LumScriptEntity& value);

template <typename... Args> constexpr int totalSlots() {
	return (stackSlots<Args>() + ... + 0);
}

template <int I, typename... Args> constexpr int prefixSlots() {
	if constexpr (I == 0) {
		return 0;
	}
	else {
		return prefixSlots<I - 1, Args...>() + stackSlots<typename ArgTypeAt<I - 1, Args...>::Type>();
	}
}

template <int I, typename... Args> constexpr int argIndex() {
	constexpr int total = totalSlots<Args...>();
	constexpr int before = prefixSlots<I, Args...>();
	constexpr int slots = stackSlots<typename ArgTypeAt<I, Args...>::Type>();
	return -(total - before - slots + 1);
}

template <typename T> struct FunctionTraits;

template <typename R, typename... Args> struct FunctionTraits<R (*)(Args...)> {
	using Result = R;
	static constexpr int arity = sizeof...(Args);
};

template <typename R, typename... Args, int... I>
static void callImpl(ls_runtime* runtime, R (*f)(Args...), Indices<I...>) {
	using Ret = typename ResultOf<R (*)(Args...)>::Type;
	if constexpr (IsSame<Ret, void>::Value) {
		(*f)(checkArg<RemoveCVR<Args>>(runtime, argIndex<I, RemoveCVR<Args>...>())...);
	}
	else {
		push(runtime, (*f)(checkArg<RemoveCVR<Args>>(runtime, argIndex<I, RemoveCVR<Args>...>())...));
	}
}

template <auto f> static void wrap(ls_runtime* runtime, ls_call_frame frame) {
	using Traits = FunctionTraits<decltype(f)>;
	callImpl(runtime, f, typename BuildIndices<-1, Traits::arity>::result{});
}

} // namespace Lumix::LumScript
