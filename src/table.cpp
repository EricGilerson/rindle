//
// Created by Eric Gilerson on 10/7/25.
//
/*==============================================================================
  File: src/table.cpp

  Purpose:
    Implements the simple columnar Table used by the builder. Handles storage
    and access patterns for numeric columns and the Date column (UTC ns).

  Responsibilities:
    - Manage column vectors, sizes, and basic integrity checks.
    - Provide efficient column views for downstream modules.

  Notes:
    - Memory ownership is clear: Table owns its storage; views are non-owning.
==============================================================================*/

#include "rivulet/table.hpp"
