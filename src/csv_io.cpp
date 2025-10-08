//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/csv_io.cpp

  Purpose:
    Implements CSV reading and writing for v1. Converts Date to UTC nanoseconds
    when present and writes per-ticker and combined outputs.

  Responsibilities:
    - Robustly parse input CSVs; report schema errors with context.
    - Write X.csv, y.csv, index.csv with consistent headers and ordering.
    - Create output directories and handle append semantics for combined outputs.

  Notes:
    - Swap internals to Parquet/Arrow later without changing the header API.
==============================================================================*/

#include "rivulet/csv_io.hpp"