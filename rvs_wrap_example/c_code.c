
void cpp_task (void);

void c_task (void)
{
   cpp_task ();
   cpp_task ();
}

int main (void)
{
   #pragma RVS add_code ("RVS_Init();");
   c_task ();
   #pragma RVS add_code ("RVS_Output();");
   return 0;
}
