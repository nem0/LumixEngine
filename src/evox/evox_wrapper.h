#pragma once

#include "core/math.h"
#include "core/metaprogramming.h"
#include "evox/capi.h"
#include <tuple> // TODO remove

namespace Lumix::Evox {

template <typename T> struct IsPointer { static constexpr bool Value = false; };
template <typename T> struct IsPointer<T*> { static constexpr bool Value = true; };

template <typename T> T readArg(ex_call_frame& frame) {
	static_assert(IsPointer<T>::Value);
	T value;
	memcpy(&value, frame.args, sizeof(value));
	frame.args += sizeof(value);
	return value;
}

template <> inline bool readArg<bool>(ex_call_frame& frame) { bool value = *frame.args != 0; ++frame.args; return value; }
template <> inline u32 readArg<u32>(ex_call_frame& frame) { u32 value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline i32 readArg<i32>(ex_call_frame& frame) { i32 value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline float readArg<float>(ex_call_frame& frame) { float value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline double readArg<double>(ex_call_frame& frame) { double value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline ex_string_view readArg<ex_string_view>(ex_call_frame& frame) { return ex_arg_read_string(&frame); }

template <typename T> void writeResult(ex_runtime*, ex_call_frame& frame, T* value) {
	EX_RESULT(frame, value);
}

template <typename T> void writeResult(ex_runtime*, ex_call_frame& frame, const T* value) {
	EX_RESULT(frame, value);
}

template <typename T> void writeResult(ex_runtime*, ex_call_frame& frame, const T& value) { EX_RESULT(frame, value); }
inline void writeResult(ex_runtime* runtime, ex_call_frame& frame, ex_string_view value) { ex_result_string(runtime, &frame, value); }
void writeResult(ex_runtime* runtime, ex_call_frame& frame, const ExEntity& value);

template <typename R, typename... Args>
static void callImpl(ex_runtime* runtime, ex_call_frame& frame, R (*f)(Args...)) {
	// Braced initialization reads arguments left-to-right, matching the script ABI.
	std::tuple<RemoveCVR<Args>...> args{readArg<RemoveCVR<Args>>(frame)...};
	if constexpr (IsSame<R, void>::Value) {
		std::apply(f, args);
	}
	else {
		writeResult(runtime, frame, std::apply(f, args));
	}
}

template <auto f> static void wrap(ex_runtime* runtime, ex_call_frame frame) {
	callImpl(runtime, frame, f);
}

} // namespace Lumix::Evox
