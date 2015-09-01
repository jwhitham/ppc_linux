#define OUTER 100
#define INNER 1000000
#pragma RVS default_instrument ("TRUE", "TIME_FUNCTIONS");
#pragma RVS instrument ("main", "FALSE");

void subtask (void)
{
   unsigned j;
   for (j = 0; j < INNER; j++) { 
      asm ("nop");
   }
}
void task1 (void)
{
   unsigned i;

   for (i = 0; i < OUTER; i++) {
      subtask ();
   }
}

void ppc_exit (void);

int _start (void)
{
   RVS_Init();
   task1 ();
   RVS_Output();
   ppc_exit ();
   return 0;
}

