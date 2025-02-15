#ifndef MEMOBS_DBGX86_H
#define MEMOBS_DBGX86_H

#include <sys/user.h>
#include <sys/types.h>

enum ww_status
{
	WFT_ERROR = -1,
	WFT_SUCCESS = 0,
	WFT_UNEXP_SIG,
	WFT_BREAK,
};

void ww_break_out(void);
enum ww_status watch_writes(int target_pid, uintptr_t addr, struct user_regs_struct *out);

#endif
