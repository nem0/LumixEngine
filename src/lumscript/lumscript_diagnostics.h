#pragma once

#include "core/string.h"
#include "lumscript/lumscript_tokenizer.h"

namespace Lumix::LumScript {

struct Diagnostics {
	explicit Diagnostics(IAllocator& allocator)
		: message(allocator)
	{}

	template <typename... Args> void error(Args&&... args) {
		if (has_error) return;
		message.append(static_cast<Args&&>(args)...);
		has_error = true;
	}

	template <typename... Args> void errorAt(Token token, Args&&... args) {
		if (has_error) return;
		char tmp[32];
		if (!token.source_name.empty()) {
			message.append(token.source_name);
			message.append(": ");
		}
		message.append("line ");
		toCString(token.line, Span(tmp));
		message.append(tmp);
		message.append(", column ");
		toCString(token.column, Span(tmp));
		message.append(tmp);
		message.append(": ");
		message.append(static_cast<Args&&>(args)...);
		has_error = true;
	}

	bool has_error = false;
	String message;
};

} // namespace Lumix::LumScript
