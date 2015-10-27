#undef TRACE_SYSTEM 
#define TRACE_SYSTEM mathemu
 
#if !defined(_TRACE_MATHEMU_H) || defined(TRACE_HEADER_MULTI_READ) 
#define _TRACE_MATHEMU_H 
 
#include <linux/tracepoint.h> 
 
TRACE_EVENT(mathemu_entry, 
 
  TP_PROTO(struct pt_regs *regs, unsigned long address, unsigned long insn),
 
  TP_ARGS(regs, address, insn), 
 
  TP_STRUCT__entry( 
  __field( unsigned long, ip ) 
  __field( unsigned long, addr  ) 
  __field( unsigned long, insn ) 
  ), 
 
  TP_fast_assign( 
  __entry->ip = regs ? instruction_pointer(regs) : 0UL; 
  __entry->addr  = address; 
  __entry->insn = insn;
  ), 
 
  TP_printk("ip=%lu addr=%lu insn=%lu", 
  __entry->ip, __entry->addr, __entry->insn) 
); 
 
TRACE_EVENT(mathemu_exit, 
 
  TP_PROTO(int eflag), 
 
  TP_ARGS(eflag), 
 
  TP_STRUCT__entry( 
  __field( int,  eflag ) 
  ), 
 
  TP_fast_assign( 
  __entry->eflag   = eflag; 
  ), 
 
  TP_printk("eflag=%d", __entry->eflag) 
); 
 
#endif /* _TRACE_MATHEMU_H */ 
/* This part must be outside protection */ 
#include <trace/define_trace.h> 

