/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : loop.c
 * Description :
 * Part of library test, runs the loopN function.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#include "librvs.h"

void loopN (unsigned count);

void run_loop (unsigned count)
{
   RVS_Ipoint (10);
   loopN (count);
   RVS_Ipoint (11);
}
