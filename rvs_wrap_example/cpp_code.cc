
#define OUTER 100
#define INNER 10000

void subtask (void)
{
   unsigned j;
   for (j = 0; j < INNER; j++) { 
      asm ("nop");
   }
}

void cpp_task (void)
{
   unsigned i;

   for (i = 0; i < OUTER; i++) {
      subtask ();
   }
}


int main (void)
{
   #pragma RVS add_code ("RVS_Init();");
   cpp_task ();
   #pragma RVS add_code ("RVS_Output();");
   return 0;
}
