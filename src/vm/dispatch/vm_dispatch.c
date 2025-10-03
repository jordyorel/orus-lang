// Orus Language Project
// ---------------------------------------------------------------------------
// File: src/vm/dispatch/vm_dispatch.c
// Author: Jordy Orel KONDA
// Copyright (c) 2025 Jordy Orel KONDA
// License: MIT License (see LICENSE file in the project root)
// Description: Implements the primary opcode dispatch loop bridging interpreter variants.


// vm_dispatch.c - Unified dispatch compilation unit
#include "../operations/vm_arithmetic.c"
#include "../operations/vm_control_flow.c"
#include "../operations/vm_string_ops.c"
#include "../operations/vm_comparison.c"
#include "vm_dispatch_goto.c"
#include "vm_dispatch_switch.c"
