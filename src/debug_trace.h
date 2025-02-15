#ifndef DEBUG_TRACE_H
#define DEBUG_TRACE_H


#ifdef DEBUG_TRACE_TO_STDERR

#include <errno.h>
#include <string.h>
#include <stdio.h>

#define debug_trace()\
	do { fprintf(stderr, "[!] Error in %s() [%s #%i]\n",\
		__func__, __FILE__, __LINE__ - 1); } while(0)

#define debug_trace_errno()\
	do { fprintf(stderr, "[!] Error in %s() [%s #%i] : %s\n",\
		__func__, __FILE__, __LINE__ - 1, strerror(errno));\
	     errno = 0; } while(0)

#define debug_warning(msg)\
	do { fprintf(stderr, "[#] Warning in %s() [%s #%i] : %s\n",\
		__func__, __FILE__, __LINE__ - 1, (msg));\
	     errno = 0; } while(0)

#define debug_info(msg)\
	do { fprintf(stderr, "[*] Info: in %s() [%s #%i] : %s\n",\
		__func__, __FILE__, __LINE__ - 1, (msg));\
	     errno = 0; } while(0)

#define debug_var_print(fmts, ...)\
	do { fprintf(stderr, "[*] Info: in %s() [%s #%i] : " fmts "\n",\
		__func__, __FILE__, __LINE__ - 1, __VA_ARGS__);\
	     errno = 0; } while(0)

#else

#define debug_trace()\
		((void)(0))

#define debug_trace_errno()\
		((void)(0))

#define debug_warning(msg)\
		((void)(0))

#define debug_info(msg)\
		((void)(0))

#define debug_var_print(fmts, ...)\
		((void)(0))

#endif //DEBUG_TRACE_TO_STDERR

#endif //DEBUG_TRACE_H
