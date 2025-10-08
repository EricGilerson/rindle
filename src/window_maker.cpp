//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/window_maker.cpp

  Purpose:
    Implements sliding-window construction and target alignment.

  Responsibilities:
    - Produce flattened, time-major windows of length L over features F.
    - Align targets to the window end row and take H future steps.
    - Populate index rows with original timestamps or ordinal indices.

  Notes:
    - Guarantees “no future leakage”: windows use rows ≤ end, targets start at end.
==============================================================================*/

#include "rivulet/window_maker.hpp"
