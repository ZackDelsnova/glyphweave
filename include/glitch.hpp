#pragma once

#include "types.hpp"
#include <vector>

// apply a row shift glitch effect to the cells, in place modifying
void apply_row_shift_glitch(std::vector<Cell>& cells, int blocks_x, int blocks_y);
