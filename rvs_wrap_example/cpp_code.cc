
#define OUTER 100
#define INNER 10000

void subtask (void)
{
   unsigned j;
   for (j = 0; j < INNER; j++) { 
      asm ("nop");
   }
}

extern "C" void cpp_task (void);

void cpp_task (void)
{
   unsigned i;

   for (i = 0; i < OUTER; i++) {
      subtask ();
   }
}

