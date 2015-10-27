#ifndef __LIBRVS_H__
#define __LIBRVS_H__


#ifdef __cplusplus
extern "C" {
#endif

#ifndef RVS_TIMER_ENTRY
/* Context switch markers in the trace. */
#define RVS_MATHEMU_ENTRY  0xfffffffd     /* mathemu entry */
#define RVS_MATHEMU_EXIT   0xfffffffc     /* mathemu exit */
#define RVS_PFAULT_ENTRY   0xfffffffb     /* page fault entry */
#define RVS_PFAULT_EXIT    0xfffffffa     /* page fault exit */
#define RVS_TIMER_ENTRY    0xfffffff9     /* timer interrupt entry */
#define RVS_TIMER_EXIT     0xfffffff8     /* timer interrupt exit */
#define RVS_SYS_ENTRY      0xfffffff7     /* syscall entry */
#define RVS_SYS_EXIT       0xfffffff6     /* syscall exit */
#define RVS_IRQ_ENTRY      0xfffffff5     /* interrupt entry */
#define RVS_IRQ_EXIT       0xfffffff4     /* interrupt exit */
#define RVS_SWITCH_FROM    0xfffffff3     /* task suspended */
#define RVS_SWITCH_TO      0xfffffff2     /* task resumed */
#define RVS_BEGIN_WRITE    0xfffffff1     /* userspace: librvs began writing trace to disk */
#define RVS_END_WRITE      0xfffffff0     /* userspace: librvs finished writing trace to disk */
#endif

void RVS_Init (void);
void RVS_Output (void);

struct rvs_uentry {
   unsigned id;
   unsigned tstamp;
};

extern struct rvs_uentry * user_pos;

static inline void RVS_Ipoint (unsigned id)
{
   unsigned l1;
   asm volatile("mfspr %0, 526" : "=r" (l1));   /* get cycles */
   user_pos->tstamp = l1;
   user_pos->id = id;

#if 0
   if (user_pos == &user_buffer[USER_BUFFER_SIZE - 1]) {
      /* flush the trace */
      user_pos->id = RVS_BEGIN_WRITE;
      user_pos ++;
      merge_buffers_now ();
      user_pos->tstamp = rvs_get_cycles ();
      user_pos->id = RVS_END_WRITE;
      user_pos ++;
      user_pos->tstamp = rvs_get_cycles ();
      user_pos->id = id;
   }
#endif
   user_pos ++;
}


#ifdef __cplusplus
};
#endif

#endif /* __LIBRVS_H__ */
