// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#include "chamber.hpp"

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    return index == 0 ? &chamber::descriptor : nullptr;
}
