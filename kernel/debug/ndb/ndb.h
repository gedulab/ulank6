/*
header for the NanoCode Debugger (NDB) by GEDU Tech. 
*/
#ifndef __NDB_H__
#define __NDB_H__

#ifdef __KERNEL__
    #include <linux/types.h>
#else
    #include <stdbool.h>
#endif

// 16-bit immediate for NDB break in, aka the HLT instruction
#define NDB_BRK_IMM_INIT         0xE000 
#define NDB_BRK_IMM_MOD_LOAD     0xE001 
#define NDB_BRK_IMM_MOD_UNLD     0xE002 
#define NDB_BRK_IMM_PROCESS_BORN 0xE003 
#define NDB_BRK_IMM_PROCESS_EXIT 0xE004 
#define NDB_BRK_IMM_THREAD_BORN  0xE005 
#define NDB_BRK_IMM_THREAD_EXIT  0xE006
#define NDB_BRK_IMM_OOPS         0xE007
#define NDB_BRK_IMM_UNKOWN       0xE008
#define NDB_BRK_IMM_MANUAL       0xE009
#define NDB_BRK_IMM_BOOKED       0xE00A

int ndb_breakin(int reason, long para1, long para2);
void ndb_check_breakin(void);
bool __init ndb_need_init_break(void);

#endif // __NDB_H__
