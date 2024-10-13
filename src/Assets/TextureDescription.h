#pragma once

#include "Common.h"

struct TextureDescription { u32 Value = 0; };
// MSB      ::::  |  ::::    ::::  | LSB
//           mip     log2   format
//          count   of size
