#include <sys/uio.h>
#include <stdint.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>

#include "wwx86.h"
#include "bitops.h"

#define DEBUG_TRACE_TO_STDERR
#include "debug_trace.h"

#define UNUSED(var) ((void)(var))
#define DR_OFFSET(dr) ((((struct user *)0)->u_debugreg) + (dr))

//https://stackoverflow.com/questions/7805782/hardware-watchpoints-how-do-they-work
//https://en.wikipedia.org/wiki/X86_debug_register
//https://wiki.osdev.org/CPU_Registers_x86
//https://stackoverflow.com/questions/75723172/how-to-get-x86-64-debug-registers-working-to-set-breakpoint-from-parent-process
/*
- dr0:
	- 0:64 = addr
- dr7:
	- 0 = 1
	- 1 = 0
	- 8 = 1
	- 17:16 = 01
	- 19:18 = 00
*/

static int set_wwatch_trap(int target_pid, uintptr_t addr);
static int remove_wwatch_trap(int target_pid);
static enum ww_status wait_for_trap(int target_pid);

static int break_out = 0;

void ww_break_out(void)
{
	__atomic_store_n(&break_out, 1, __ATOMIC_SEQ_CST);

}

enum ww_status watch_writes(int target_pid, uintptr_t addr, struct user_regs_struct *out)
{
	UNUSED(addr);
	long err;
	enum ww_status ret;

	err = set_wwatch_trap(target_pid, addr);
	if(err != 0) {
		debug_trace();
		return -1;
	}

	ret = wait_for_trap(target_pid);

	err = ptrace(PTRACE_GETREGS, target_pid, NULL, out);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	err = remove_wwatch_trap(target_pid);
	if(err != 0) {
		debug_trace();
		return -1;
	}

	return ret;
}

static int stop_proc(int target_pid)
{
	int status;
	int err;

	err = kill(target_pid, SIGSTOP);
	if(err < 0) {
		debug_trace_errno();
		return -1;
	}
	err = waitpid(target_pid, &status, WUNTRACED);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}
	if(WIFSTOPPED(status) == 0) {
		debug_trace_errno();
		return -1;
	}
	if(WSTOPSIG(status) != SIGSTOP) {
		debug_warning("Unexpected signal received");
		return -1;
	}

	return 0;
}

static enum ww_status wait_for_trap(int target_pid)
{
	int err;
	long ret;
	int status;
	
	ret = ptrace(PTRACE_CONT, target_pid, NULL, (void *)SIGCONT) ;
	if(ret == -1) {
		debug_trace_errno();
		return WFT_ERROR;
	}

	err = 0;
	while(err == 0 && break_out == 0) {
		err = waitpid(target_pid, &status, WUNTRACED | WNOHANG);
	}

	if(err == -1) {
		debug_trace_errno();
		return WFT_ERROR;
	}
	else if(break_out == 1) {
		__atomic_store_n(&break_out, 0, __ATOMIC_SEQ_CST);
		err = stop_proc(target_pid);
		if(err != 0) {
			debug_trace();
			return WFT_ERROR;
		}
		return WFT_BREAK;
	}

	if(WIFSTOPPED(status) == 0) {
		debug_trace_errno();
		return WFT_ERROR;
	}

	if(WSTOPSIG(status) != SIGTRAP) {
		debug_warning("Unexpected signal received");
		return WFT_UNEXP_SIG;
	}

	ret = ptrace(PTRACE_PEEKUSER, target_pid, DR_OFFSET(6), NULL);
	if(ret == -1) {
		debug_trace_errno();
		return WFT_ERROR;
	}

	return WFT_SUCCESS;
}

static int set_wwatch_trap(int target_pid, uintptr_t addr)
{
	long err;
	long ret;
	unsigned long long dr7;
	unsigned long long dr6;
	unsigned long long dr0;

	ret = ptrace(PTRACE_PEEKUSER, target_pid, DR_OFFSET(7), NULL);
	if(ret == -1) {
		debug_trace_errno();
		return -1;
	}

	dr7 = (unsigned long long)ret;

	dr0 = addr;	

	BIT_SET(dr7, 0);
	BIT_CLEAR(dr7, 1);

	BIT_SET(dr7, 8);
	BIT_SET(dr7, 9);

	//BIT_SET(dr7, 13);

	BIT_SET(dr7, 16);
	BIT_CLEAR(dr7, 17);

	BIT_SET(dr7, 18);
	BIT_SET(dr7, 19);

	dr6 = 0;
	
	err = ptrace(PTRACE_POKEUSER, target_pid, DR_OFFSET(0), dr0);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	err = ptrace(PTRACE_POKEUSER, target_pid, DR_OFFSET(7), dr7);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	err = ptrace(PTRACE_POKEUSER, target_pid, DR_OFFSET(6), dr6);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	return 0;
}

static int remove_wwatch_trap(int target_pid)
{
	long err;
	long ret;
	unsigned long long dr7;
	unsigned long long dr6;

	ret = ptrace(PTRACE_PEEKUSER, target_pid, DR_OFFSET(7), NULL);
	if(ret == -1) {
		debug_trace_errno();
		return -1;
	}

	dr7 = (unsigned long long)ret;

	BIT_CLEAR(dr7, 0);
	BIT_CLEAR(dr7, 1);
	BIT_CLEAR(dr7, 8);
	BIT_CLEAR(dr7, 9);
	BIT_CLEAR(dr7, 13);
	BIT_CLEAR(dr7, 16);
	BIT_CLEAR(dr7, 17);
	BIT_CLEAR(dr7, 18);
	BIT_CLEAR(dr7, 19);

	dr6 = 0;
	
	err = ptrace(PTRACE_POKEUSER, target_pid, DR_OFFSET(7), dr7);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	err = ptrace(PTRACE_POKEUSER, target_pid, DR_OFFSET(6), dr6);
	if(err == -1) {
		debug_trace_errno();
		return -1;
	}

	return 0;
}


