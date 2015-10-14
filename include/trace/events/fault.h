#undef TRACE_SYSTEM 
#define TRACE_SYSTEM fault 
 
#if !defined(_TRACE_FAULT_H) || defined(TRACE_HEADER_MULTI_READ) 
#define _TRACE_FAULT_H 
 
#include <linux/tracepoint.h> 
 
TRACE_EVENT(page_fault_entry, 
 
  TP_PROTO(struct pt_regs *regs, unsigned long address, 
  int write_access), 
 
  TP_ARGS(regs, address, write_access), 
 
  TP_STRUCT__entry( 
  __field( unsigned long, ip ) 
  __field( unsigned long, addr  ) 
  __field( uint8_t, write ) 
  ), 
 
  TP_fast_assign( 
  __entry->ip = regs ? instruction_pointer(regs) : 0UL; 
  __entry->addr  = address; 
  __entry->write = !!write_access; 
  ), 
 
  TP_printk("ip=%lu addr=%lu write_access=%d", 
  __entry->ip, __entry->addr, __entry->write) 
); 
 
TRACE_EVENT(page_fault_exit, 
 
  TP_PROTO(int result), 
 
  TP_ARGS(result), 
 
  TP_STRUCT__entry( 
  __field( int,  res   ) 
  ), 
 
  TP_fast_assign( 
  __entry->res   = result; 
  ), 
 
  TP_printk("result=%d", __entry->res) 
); 
 
#endif /* _TRACE_FAULT_H */ 
/* This part must be outside protection */ 
#include <trace/define_trace.h> 

