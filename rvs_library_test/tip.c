/*=========================================================================
 * RapiTime    : a tool for Measurement-Based Execution Time Analysis
 * Module      : rvs_library for PPC Linux
 * File        : tip.c
 * Description :
 * Part of library test, intended to invoke the inline RVS_Ipoint function
 * in various contexts.
 *
 * Copyright (c) 2016 Rapita Systems Ltd.               All rights reserved
 *=========================================================================
*/
#include "librvs.h"

void test_ipoint_1 (int v)
{
   RVS_Ipoint (v);
}

int one = 1;
int always = 1;
int value = 1999;
int rotator = 0;

void test_ipoint_2 (void)
{
   int i;
   for (i = 0; (i < one) && always; i++) {
      RVS_Ipoint (1999);
      always ++;
   }
}

void test_ipoint_3 (void)
{
   switch (rotator % 4) {
      case 0:
         RVS_Ipoint (1999);
         break;
      case 1:
         RVS_Ipoint (value);
         break;
      case 2:
         test_ipoint_2 ();
         break;
      default:
         rotator ++;
         test_ipoint_1 (value);
         break;
   }
   rotator ++;
}



