#pragma once

#include "core/math.h"
#include "core/metaprogramming.h"
#include "lumscript/capi.h"
#include <tuple> // TODO remove

namespace Lumix::LumScript {

template <typename T> struct IsPointer { static constexpr bool Value = false; };
template <typename T> struct IsPointer<T*> { static constexpr bool Value = true; };

template <typename T> T readArg(ls_call_frame& frame) {
	static_assert(IsPointer<T>::Value);
	T value;
	memcpy(&value, frame.args, sizeof(value));
	frame.args += sizeof(value);
	return value;
}

template <> inline bool readArg<bool>(ls_call_frame& frame) { bool value = *frame.args != 0; ++frame.args; return value; }
template <> inline u32 readArg<u32>(ls_call_frame& frame) { u32 value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline i32 readArg<i32>(ls_call_frame& frame) { i32 value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline float readArg<float>(ls_call_frame& frame) { float value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline double readArg<double>(ls_call_frame& frame) { double value; memcpy(&value, frame.args, sizeof(value)); frame.args += sizeof(value); return value; }
template <> inline ls_string_view readArg<ls_string_view>(ls_call_frame& frame) { return ls_arg_read_string(&frame); }

template <typename T> void writeResult(ls_runtime*, ls_call_frame& frame, T* value) {
	LS_RESULT(frame, value);
}

template <typename T> void writeResult(ls_runtime*, ls_call_frame& frame, const T* value) {
	LS_RESULT(frame, value);
}

template <typename T> void writeResult(ls_runtime*, ls_call_frame& frame, const T& value) { LS_RESULT(frame, value); }
inline void writeResult(ls_runtime* runtime, ls_call_frame& frame, ls_string_view value) { ls_result_string(runtime, &frame, value); }
void writeResult(ls_runtime* runtime, ls_call_frame& frame, const LsEntity& value);

template <typename R, typename... Args>
static void callImpl(ls_runtime* runtime, ls_call_frame& frame, R (*f)(Args...)) {
	// Braced initialization reads arguments left-to-right, matching the script ABI.
	std::tuple<RemoveCVR<Args>...> args{readArg<RemoveCVR<Args>>(frame)...};
	if constexpr (IsSame<R, void>::Value) {
		std::apply(f, args);
	}
	else {
		writeResult(runtime, frame, std::apply(f, args));
	}
}

template <auto f> static void wrap(ls_runtime* runtime, ls_call_frame frame) {
	callImpl(runtime, frame, f);
}

} // namespace Lumix::LumScript
