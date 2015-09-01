#ifndef __LIBRVS_H__
#define __LIBRVS_H__


#ifdef __cplusplus
extern "C" {
#endif

/* Markers to indicate time spent writing trace data */
#define RVS_BEGIN_WRITE		0xfffffff1
#define RVS_END_WRITE		0xfffffff0

void RVS_Init (void);
void RVS_Output (void);
void RVS_Ipoint (unsigned id);

#ifdef __cplusplus
};
#endif

#endif /* __LIBRVS_H__ */
