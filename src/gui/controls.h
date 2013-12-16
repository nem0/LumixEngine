#pragma once


#include "core/lux.h"


namespace Lux
{

namespace UI
{

class Block;
class Gui;


LUX_GUI_API Block* createComboBox(int x, int y, Block* parent, Gui* gui);
LUX_GUI_API Block* createButton(const char* label, int x, int y, Block* parent, Gui* gui);
LUX_GUI_API Block* createTextBox(int x, int y, Block* parent, Gui* gui);

LUX_GUI_API Block& addComboboxItem(Block& cb, Block& item);

} // ~namespace UI

} // ~namespace Lux