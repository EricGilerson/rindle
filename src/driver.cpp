//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/driver.cpp

  Purpose:
    Wires catalog, csv_io, clean, window_maker, and manifest into a single run.

  Responsibilities:
    - Iterate tickers: read → clean → window → write per-ticker & combined.
    - Handle error propagation and summary reporting.
    - Write manifest.json at the end with final counts and time mode.

  Notes:
    - Keeps the control flow and logging in one place; no heavy logic inside.
==============================================================================*/

#include "rivulet/driver.hpp"

