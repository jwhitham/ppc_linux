#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/io.h>
#include <linux/pm.h>
#include <linux/tracepoint.h>
#include <trace/events/syscalls.h>
#include <trace/syscall.h>
#include <asm/pgtable.h>
#include <asm/trace.h>

#include "rvs.h"


/* Use the preempt_notifiers interface. */
#define DECLARE_NOTIFIER(name)	struct preempt_notifier name

/* Interface: we only support reading the trace from inside the
 * traced task; this could turn out to be impractical for
 * multithreaded applications or for more general uses of the
 * tracer.  The drawback of allowing external tasks to access
 * the traces is that synchronisation becomes more complicated,
 * and locks on the fastpath may become necessary (depending on
 * the desired access patterns).
 * The current simplified ioctl() interface is decoupled from the
 * operations on the traces, in case we need to switch to a more
 * complex interface.
 */

/* Hash table shift: the number of tasks is expected to be small,
 * the hash table is dimensioned accordingly. */
#define RVS_HASH_SHIFT		6
#define RVS_HASH_SIZE		(1U << RVS_HASH_SHIFT)
#define RVS_HASH_MASK		((1U << RVS_HASH_SHIFT) - 1)

/* Default buffer size (entries). */
#define RVS_DFLT_BUF_SHIFT	(16)

/*
 * Object containing the global state of the tracer and the traces
 * themselves.  The hash table, as of now, is only used to prevent
 * the same task to be traced multiple times; it doesn't add to
 * the tracing overhead and is likely to be useful in case we
 * need to extend the interface.
 */
struct rvs_tracer {
   spinlock_t hash_lock;		/* Synchronise writers. */
   struct hlist_head hash[RVS_HASH_SIZE]; /* Hash table. */
};

/* A trace entry. */
struct rvs_entry {
   u32 tid;			/* Entering/exiting thread ID */ 
   u32 tstamp;			/* Timestamp. */
};

/* log2(sizeof(struct rvs_entry)) */
#define RVS_ENTRY_SHIFT			3
#define RVS_ENTRY_SIZE			(1 << RVS_ENTRY_SHIFT)
#define RVS_ENTRY_MASK			((1 << RVS_ENTRY_SHIFT) - 1)

/* Trace for a single task. */
struct rvs_task {
   DECLARE_NOTIFIER(notifier);	/* Context switch notifier. */
   int in_progress;		/* Tracing in progress. */

   unsigned buf_shift;		/* Buffer size (log2, in entries). */

   unsigned first, last;		/* Ring buffer pointers. */
   struct rvs_entry *entries;	/* Buffer entries.  The double
                * allocation for rvs_task and
                * entries[] is not ideal, but
                * the allocation constraints
                * imposed by using RCU limit
                * our reallocation possibilities. */

   struct rvs_stats stats;		/* Collect tracing statistics. */

   pid_t tid;			/* Thread ID of the traced task. */
   struct hlist_node hlist;	/* Hash list entry. */
   unsigned cpu_id;
};

/* Global tracer state (just the hash table for now). */
static struct rvs_tracer rvs_tracer;

static inline void rvs_add_entry(struct rvs_task *taskp, pid_t tid, u32 tstamp)
{
   unsigned pos = taskp->last & ((1U << taskp->buf_shift) - 1);

   /* We should count the missed events. */
   if (unlikely(taskp->last == taskp->first + (1U << taskp->buf_shift))) {
      taskp->stats.missed++;
      return;
   }
   taskp->entries[pos].tid = tid;
   taskp->entries[pos].tstamp = tstamp;
   taskp->last++;

   /* XXX: verify cache line size and if this causes an extra miss */
   taskp->stats.written++;
}

static void rvs_sched_in(struct preempt_notifier *pn, int cpu)
{
   struct rvs_task *taskp = container_of(pn, struct rvs_task, notifier);

   if (taskp->in_progress) {
      rvs_add_entry(taskp, RVS_SWITCH_TO, rvs_get_cycles());
      taskp->cpu_id = smp_processor_id();
   }
}

static void rvs_sched_out(struct preempt_notifier *pn,
           struct task_struct *next)
{
   u32 cycles = rvs_get_cycles();
   struct rvs_task *taskp = container_of(pn, struct rvs_task, notifier);

   if (taskp->in_progress) {

      if (!current->nsproxy) {
         /* thread has been killed (uncleanly) */
         printk(KERN_ERR "rvs: thread died without "
            "calling RVS_thread_finish\n");
         return;
      }

      rvs_add_entry(taskp, RVS_SWITCH_FROM, cycles);
   }
}

static void rvs_timer_entry (void *data, struct pt_regs *regs)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_TIMER_ENTRY, rvs_get_cycles());
   }
}

static void rvs_timer_exit (void *data, struct pt_regs *regs)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_TIMER_EXIT, rvs_get_cycles());
   }
}

static void rvs_irq_entry (void *data, struct pt_regs *regs)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_IRQ_ENTRY, rvs_get_cycles());
   }
}

static void rvs_irq_exit (void *data, struct pt_regs *regs)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_IRQ_EXIT, rvs_get_cycles());
   }
}

static void rvs_sys_entry (void *data, struct pt_regs *regs, long id)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_SYS_ENTRY, rvs_get_cycles());
   }
}

static void rvs_sys_exit (void *data, struct pt_regs *regs, long id)
{
   struct rvs_task *taskp = (struct rvs_task *) data;

   if (taskp->in_progress && (taskp->cpu_id == smp_processor_id())) {
      rvs_add_entry(taskp, RVS_SYS_EXIT, rvs_get_cycles());
   }
}

static struct preempt_ops rvs_preempt_ops = {
   .sched_in = rvs_sched_in,
   .sched_out = rvs_sched_out,
};

static void rvs_init_notifiers(struct rvs_task *taskp)
{
   preempt_notifier_init(&taskp->notifier, &rvs_preempt_ops);
   preempt_notifier_register(&taskp->notifier);
   register_trace_timer_interrupt_entry(rvs_timer_entry, taskp);
   register_trace_timer_interrupt_exit(rvs_timer_exit, taskp);
   register_trace_irq_entry(rvs_irq_entry, taskp);
   register_trace_irq_exit(rvs_irq_exit, taskp);
   register_trace_sys_enter(rvs_sys_entry, taskp);
   register_trace_sys_exit(rvs_sys_exit, taskp);
}

static void rvs_clear_notifiers(struct rvs_task *taskp)
{
   preempt_notifier_unregister(&taskp->notifier);
   unregister_trace_timer_interrupt_entry(rvs_timer_entry, taskp);
   unregister_trace_timer_interrupt_exit(rvs_timer_exit, taskp);
   unregister_trace_irq_entry(rvs_irq_entry, taskp);
   unregister_trace_irq_exit(rvs_irq_exit, taskp);
   unregister_trace_sys_enter(rvs_sys_entry, taskp);
   unregister_trace_sys_exit(rvs_sys_exit, taskp);
   tracepoint_synchronize_unregister();
}

static inline unsigned rvs_hash_bucket(pid_t tid)
{
   return tid % RVS_HASH_MASK;
}

static inline struct rvs_task *rvs_lookup(pid_t tid)
{
   struct rvs_tracer *tracer = &rvs_tracer;
   int bkt = rvs_hash_bucket(tid);
   struct rvs_task *taskp = NULL;

   rcu_read_lock();
   hlist_for_each_entry(taskp, &tracer->hash[bkt], hlist) {
      if (taskp->tid == tid)
         break;
   }
   rcu_read_unlock();

   return taskp;
}

static void rvs_task_attach(struct rvs_task *taskp)
{
   struct rvs_tracer *tracer = &rvs_tracer;
   int bkt = rvs_hash_bucket(taskp->tid);

   spin_lock(&tracer->hash_lock);
   hlist_add_head_rcu(&taskp->hlist, &tracer->hash[bkt]);
   spin_unlock(&tracer->hash_lock);
}

static void rvs_task_detach(struct rvs_task *taskp)
{
   struct rvs_tracer *tracer = &rvs_tracer;

   spin_lock(&tracer->hash_lock);
   hlist_del_rcu(&taskp->hlist);
   spin_unlock(&tracer->hash_lock);
}

static int rvs_task_reset(struct rvs_task *taskp)
{
   /* Can't reset while tracing is in progress. */
   if (taskp->in_progress)
      return -EBUSY;

   taskp->first = 0;
   taskp->last = 0;
   memset(&taskp->stats, 0, sizeof(struct rvs_stats));

   return 0;
}

static int rvs_task_set_bufshift(struct rvs_task *taskp, unsigned buf_shift)
{
   struct rvs_entry *entries;
   unsigned buf_size = 1U << buf_shift;

   /* Can't set buffer size while tracing is in progress. */
   if (taskp->in_progress)
      return -EBUSY;

   entries = kmalloc(buf_size*sizeof(struct rvs_entry), GFP_KERNEL);
   if (!entries)
      return -ENOMEM;

   kfree(taskp->entries);
   taskp->entries = entries;
   taskp->buf_shift = buf_shift;

   /* Resizing the buffer acts implicitely as a reset... */
   taskp->first = 0;
   taskp->last = 0;

   return 0;
}

static long rvs_ioctl_get_version(struct file *filp, void __user *argp)
{
   __u32 version = RVS_API_VERSION;

   if (copy_to_user(argp, &version, sizeof(__u32)))
      return -EFAULT;
   return 0;
}

static long rvs_ioctl_reset(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;

   /* We only support permanently attached tasks, filp->private_data
    * is always expected to be valid. */
   BUG_ON(!taskp);

   /* We don't expect any arguments. */
   if (argp)
      return -EINVAL;

   return rvs_task_reset(taskp);
}

static long rvs_ioctl_set_bufshift(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;
   u32 buf_shift;

   BUG_ON(!taskp);

   if (copy_from_user(&buf_shift, argp, sizeof(__u32)))
      return -EFAULT;

   return rvs_task_set_bufshift(taskp, buf_shift);
}

static long rvs_ioctl_get_bufshift(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;
   u32 buf_shift;

   BUG_ON(!taskp);

   buf_shift = taskp->buf_shift;
   if (copy_to_user(argp, &buf_shift, sizeof(__u32)))
      return -EFAULT;
   return 0;
}

static long rvs_enable(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;

   BUG_ON(!taskp);

   if (argp)
      return -EINVAL;

   taskp->in_progress = 1;
   return 0;
}

static long rvs_disable(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;

   BUG_ON(!taskp);

   if (argp)
      return -EINVAL;
   taskp->in_progress = 0;
   return 0;
}

static long rvs_get_stats(struct file *filp, void __user *argp)
{
   struct rvs_task *taskp = filp->private_data;

   BUG_ON(!taskp);

   if (copy_to_user(argp, &taskp->stats, sizeof(struct rvs_stats)))
      return -EFAULT;

   return 0;
}

static struct rvs_task *rvs_task_create(struct task_struct *task)
{
   struct rvs_task *taskp;
   unsigned buf_size;

   taskp = kmalloc(sizeof(struct rvs_task), GFP_KERNEL);
   if (!taskp)
      return NULL;

   taskp->tid = task_pid_vnr(task);
   INIT_HLIST_NODE(&taskp->hlist);

   taskp->in_progress = 0;
   taskp->cpu_id = smp_processor_id();

   taskp->buf_shift = RVS_DFLT_BUF_SHIFT;
   buf_size = 1U << taskp->buf_shift;

   taskp->first = taskp->last = 0;
   taskp->entries = kmalloc(buf_size*sizeof(struct rvs_entry), GFP_KERNEL);
   if (!taskp->entries)
      goto out_free;
   memset(&taskp->stats, 0, sizeof(struct rvs_stats));

   return taskp;

out_free:
   kfree(taskp);
   return NULL;
}

static void rvs_task_destroy(struct rvs_task *taskp)
{
   /* Make sure no pointer to taskp is in use anymore. */
   synchronize_rcu();

   kfree(taskp->entries);
   kfree(taskp);
}

/* Copy buffer data to userspace.  This is a linearisation of the buffer:
 * xxxxxx|---------------|----------|xxxxxx
 *       ^               ^          ^
 *     first       wrapping point  last
 * Depending on where lpos happens to fall, we copy the relevant parts
 * of the trace into the user provided buffer.
 */
static ssize_t rvs_read(struct file *filp, char __user *buf,
         size_t count, loff_t *lpos)
{
   unsigned buf_bytes, pos, start, end, entries;
   struct rvs_task *taskp = filp->private_data;
   char *ebuf = (void *)taskp->entries;
   ssize_t bytes, wbytes = 0;
   int saved_in_progress;

   /* We expect current to be the owner of the trace.  This could
    * happen for example if a file descriptor for the tracer is
    * inherited from a parent process. */
   if (task_pid_vnr(current) != taskp->tid) {
      printk(KERN_INFO "rvs: thread data for %u read from "
            "thread %u\n", 
            taskp->tid,
            task_pid_vnr(current));
      /* return -EINVAL; */
   }

   /* Prevent trace event from overwriting the buffer while we're
    * reading: this is a safety measure, to prevent the indices from
    * changing under our feet, but, at the same time, anything that
    * happens inside rvs_read() should not be accounted to the
    * application. */
   saved_in_progress = taskp->in_progress;
   taskp->in_progress = 0;

   /* We don't allow seeks on the file (this doesn't allow multiple
    * readers, but simplifies the ring management).  This should
    * never happen, because of no_llseek() in the file ops, so
    * we should scream out loud when it does. */
   WARN_ON_ONCE(*lpos != (taskp->first << RVS_ENTRY_SHIFT));

   /* The count must be aligned to a sample. */
   if ((count & RVS_ENTRY_MASK)) {
      printk(KERN_DEBUG "rvs1: bad count %lld\n", (long long)count);
      return -EINVAL;
   }

   /* Offsets into the current window (in bytes). */
   pos = taskp->first << RVS_ENTRY_SHIFT;
   buf_bytes = 1U << (taskp->buf_shift + RVS_ENTRY_SHIFT);
   start = pos & (buf_bytes - 1);

   /* min(pos+count, taskp->last << RVS_ENTRY_SHIFT), taking into
    * account the wraparound at 2^32, to support traces larger than
    * 2GB. */
   if ((s32)(pos+count) - (s32)(taskp->last << RVS_ENTRY_SHIFT) < 0)
      end = pos + count;
   else
      end = taskp->last << RVS_ENTRY_SHIFT;
   end &= buf_bytes - 1;

   /* Check for wraparound (inside the window boundaries). */
   if (start > end) {
      wbytes = buf_bytes - start;
      if (copy_to_user(buf, ebuf+start, wbytes))
         return -EFAULT;
      start = 0;
   }

   bytes = end - start;
   if (copy_to_user(buf+wbytes, ebuf+start, bytes))
      return -EFAULT;

   *lpos += bytes + wbytes;
   entries = (bytes + wbytes) >> RVS_ENTRY_SHIFT;
   taskp->first += entries;
   taskp->stats.read += entries;
   taskp->in_progress = saved_in_progress;

   return bytes + wbytes;
}

static int rvs_mmap(struct file *file, struct vm_area_struct *vma)
{
   return -EINVAL;
}

static int rvs_open(struct inode *inode, struct file *filp)
{
   struct rvs_task *taskp;

   taskp = rvs_lookup(task_pid_vnr(current));
   /* This task is already registered. */
   if (taskp)
      return -EBUSY;

   /* At this point nobody will try to insert a task with the
    * same tid, so the lookup and the insertion don't need to
    * be atomic. */
   taskp = rvs_task_create(current);
   if (!taskp)
      return -ENOMEM;

   filp->private_data = taskp;
   rvs_task_attach(taskp);
   rvs_init_notifiers(taskp);

   return 0;
}

static int rvs_release(struct inode *inode, struct file *filp)
{
   struct rvs_task *taskp = filp->private_data;

   rvs_clear_notifiers(taskp);
   rvs_task_detach(taskp);
   rvs_task_destroy(taskp);

   return 0;
}

static long rvs_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
   void __user *argp = (void __user *)arg;
   long r = -EINVAL;

   switch (ioctl) {
   case RVS_GET_VERSION:
      r = rvs_ioctl_get_version(filp, argp);
      break;
   case RVS_RESET:
      r = rvs_ioctl_reset(filp, argp);
      break;
   case RVS_SET_BUFSHIFT:
      r = rvs_ioctl_set_bufshift(filp, argp);
      break;
   case RVS_GET_BUFSHIFT:
      r = rvs_ioctl_get_bufshift(filp, argp);
      break;
   case RVS_ENABLE:
      r = rvs_enable(filp, argp);
      break;
   case RVS_DISABLE:
      r = rvs_disable(filp, argp);
      break;
   case RVS_GET_STATS:
      r = rvs_get_stats(filp, argp);
   default:
      break;
   }
   return r;
}

/* Character device interface. */
static struct file_operations rvs_chardev_ops = {
   .owner = THIS_MODULE,
   .read = rvs_read,
   .open = rvs_open,
   .mmap = rvs_mmap,
   .release = rvs_release,
   .unlocked_ioctl = rvs_ioctl,
   .llseek = no_llseek,
};

static struct miscdevice rvs_dev = {
   MISC_DYNAMIC_MINOR,
   "rvs",
   &rvs_chardev_ops,
};

static void rvs_init_cpu(void *info)
{
   printk(KERN_INFO "rvs: init on CPU %d\n", smp_processor_id());
   rvs_init_arch();
}

static int __init rvs_init(void)
{
   struct rvs_tracer *tracer = &rvs_tracer;
   int i, r;

   BUILD_BUG_ON(sizeof(struct rvs_entry) != RVS_ENTRY_SIZE);

   preempt_disable();
   printk(KERN_INFO "rvs: loading tracer.\n");

   /* Init on all other CPUs */
   r = smp_call_function(rvs_init_cpu, NULL, 1);
   if (r) {
      printk(KERN_ERR "rvs: failed cpu initialisation\n");
      preempt_enable();
      goto out;
   }

   /* Init on the current cpu */
   printk(KERN_INFO "rvs: init on CPU %d\n", smp_processor_id());
   rvs_init_arch();

   preempt_enable();


   r = misc_register(&rvs_dev);
   if (r) {
      printk(KERN_ERR "rvs: failed registering character device\n");
      goto out;
   }

   spin_lock_init(&tracer->hash_lock);
   for (i = 0; i < RVS_HASH_SIZE; i++)
      INIT_HLIST_HEAD(&tracer->hash[i]);
out:
   return r;
}

static void __exit rvs_exit(void)
{
   printk(KERN_INFO "rvs: unloading tracer\n");
   misc_deregister(&rvs_dev);
}

module_init(rvs_init);
module_exit(rvs_exit);

MODULE_LICENSE("GPL");
