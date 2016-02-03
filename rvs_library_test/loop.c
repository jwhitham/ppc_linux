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
unsigned loopN2 (unsigned count);

void explosion (unsigned r3, unsigned r4, unsigned r5);

void run_loop (unsigned count)
{
   RVS_Ipoint (10);
   loopN (count);
   RVS_Ipoint (11);
}

void run_major_test_loop (unsigned count)
{
	unsigned rc;
   RVS_Ipoint (20);
   rc = loopN2 (count);
   RVS_Ipoint (21);
	if (rc != 0) {
		explosion (rc, ~0, ~0);
	}
}
