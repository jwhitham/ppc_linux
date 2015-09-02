#define RVS

#ifdef __cplusplus
extern "C" void RVS_Ipoint(unsigned int);
#else
void RVS_Ipoint(unsigned int);
#endif

#define RVS_I(_I)     RVS_Ipoint(_I)
#define RVS_E(_I,_E)  (RVS_I(_I),_E)
#define RVS_C(_I,_C)  (RVS_E(_I,0)?_C:_C)
#define RVS_T(_I)     RVS_E(_I,1)
#define RVS_F(_I)     RVS_E(_I,0)
