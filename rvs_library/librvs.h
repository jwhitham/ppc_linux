#ifndef __LIBRVS_H__
#define __LIBRVS_H__


#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVS_TIMER_ENTRY
/* Context switch markers in the trace. */
#define RVS_TIMER_ENTRY    0xffffffff     /* timer interrupt entry */
#define RVS_TIMER_EXIT     0xfffffffe     /* timer interrupt exit */
#define RVS_SWITCH_FROM    0xfffffff3     /* task suspended */
#define RVS_SWITCH_TO      0xfffffff2     /* task resumed */
#define RVS_BEGIN_WRITE    0xfffffff1     /* librvs began writing trace to disk */
#define RVS_END_WRITE      0xfffffff0     /* librvs finished writing trace to disk */
#endif

void RVS_Init (void);
void RVS_Output (void);
void RVS_Ipoint (unsigned id);

#ifdef __cplusplus
};
#endif

#endif /* __LIBRVS_H__ */
