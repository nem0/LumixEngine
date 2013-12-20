#pragma once


#include "core/lux.h"


namespace Lux
{

namespace UI
{

class Block;
class Gui;


LUX_GUI_API Block* createComboBox(float x, float y, Block* parent, Gui& gui);
LUX_GUI_API Block* createButton(const char* label, float x, float y, Block* parent, Gui& gui);
LUX_GUI_API Block* createTextBox(float x, float y, Block* parent, Gui& gui);

LUX_GUI_API Block& addComboboxItem(Block& cb, Block& item);

} // ~namespace UI

} // ~namespace Lux