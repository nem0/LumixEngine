#pragma once

#include "core/string.h"

namespace Lumix {

struct LUMIX_EDITOR_API TextFilter {
	bool isActive() const { return count != 0; }
	void clear() { count = 0; filter[0] = 0; }
	bool pass(StringView text) const;
	// if filter is empty, returns 1
	// returns 0 if does not pass, otherwise returns score
	u32 passWithScore(StringView text) const;
	void build();
	// show filter input, returns true if filter changed
	// add `focus_action`'s shortcut to label
	bool gui(const char* label, float width = -1, bool set_keyboard_focus = false, Action* focus_action = nullptr);

	char filter[128] = "";
	StringView subfilters[8];
	u32 count = 0;
};

}