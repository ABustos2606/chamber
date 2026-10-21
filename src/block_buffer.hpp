// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Alejandro Bustos
#pragma once

#include "elements/dsp/dsp.h"

#include <algorithm>
#include <cstdint>

namespace chamber {

// Gathers frames into Elements' fixed 16-frame blocks, whatever slices the
// host runs, so the voice and its control reads happen at the same frames
// for any slicing. Outputs lag inputs by one block.
struct BlockBuffer {
    static constexpr uint32_t kSize = static_cast<uint32_t>(elements::kMaxBlockSize);

    float blow[kSize];
    float strike[kSize];
    float main[kSize];
    float aux[kSize];
    uint32_t position;

    void reset()
    {
        std::fill(blow, blow + kSize, 0.0f); std::fill(strike, strike + kSize, 0.0f);
        std::fill(main, main + kSize, 0.0f); std::fill(aux, aux + kSize, 0.0f);
        position = 0;
    }
};

} // namespace chamber
