#include "action.h"
#include <imgui/IconsFontAwesome5.h>

namespace Lumix {

CommonActions::CommonActions() 
	: save{"Common", "Save", "Save", "save", ICON_FA_SAVE}
	, undo{"Common", "Undo", "Undo", "undo", ICON_FA_UNDO}
	, redo{"Common", "Redo", "Redo", "redo", ICON_FA_REDO}
	, del{"Common", "Delete", "Delete", "delete", ICON_FA_MINUS_SQUARE}
	
	, cam_orbit{"Camera", "Orbit", "Orbit with RMB", "orbit_rmb", ""}
	, cam_forward{"Camera", "Move forward", "Move forward", "camera_move_forward", ""}
	, cam_backward{"Camera", "Move back", "Move backward", "camera_move_back", ""}
	, cam_left{"Camera", "Move left", "Move left", "camera_move_left", ""}
	, cam_right{"Camera", "Move right", "Move right", "camera_move_right", ""}
	, cam_up{"Camera", "Move up", "Move up", "camera_move_up", ""}
	, cam_down{"Camera", "Move down", "Move down", "camera_move_down", ""}
	
	, select_all{"Common", "Select all", "Select all", "select_all", ""}
	, rename{"Common", "Rename", "Rename", "rename", ""}
	, copy{"Common", "Copy", "Copy", "copy", ICON_FA_CLIPBOARD}
	, paste{"Common", "Paste", "Paste", "paste", ICON_FA_PASTE}
	, close_window{"Common", "Close", "Close window", "close_window", ""}
	, open_externally{"Common", "Open externally", "Open externally", "open_externally", ICON_FA_EXTERNAL_LINK_ALT}
	, view_in_browser{"Common", "View in browser", "View in asset browser", "view_in_asset_browser", ICON_FA_SEARCH}
{}

}