#pragma once


namespace Lumix
{


struct ClientMessageType
{
	enum 
	{
		POINTER_DOWN = 1,
		POINTER_MOVE,
		POINTER_UP,
		PROPERTY_SET,
		MOVE_CAMERA,			// 5
		SAVE,
		LOAD,
		ADD_COMPONENT = 8,
		GET_PROPERTIES = 9,
		REMOVE_COMPONENT = 10,
		ADD_ENTITY,				// 11
		TOGGLE_GAME_MODE,		// 12
		GET_POSITION,			// 13
		SET_POSITION,			// 14
		REMOVE_ENTITY,			// 15
		SET_EDIT_MODE,			// 16
		EDIT_SCRIPT,			// 17
								// 18
		NEW_UNIVERSE = 19,		// 19
		LOOK_AT_SELECTED = 20,	// 20
		STOP_GAME_MODE,			// 21
	};
};


} // ~namespace Lumix
