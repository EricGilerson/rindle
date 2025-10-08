//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/clean.cpp

  Purpose:
    Implements the minimal, leakage-aware cleaning stage.

  Responsibilities:
    - Enforce presence and numeric type of required columns.
    - Sort by Date (UTC) if provided; otherwise leave row order intact.
    - Drop rows with missing values in any required feature/target.

  Notes:
    - Records counts of dropped rows and whether sorting occurred for the manifest.
==============================================================================*/

#include "rivulet/clean.hpp"