#define _GNU_SOURCE
#include <signal.h>
#include <dlfcn.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>

static int  (*orig_sigaction)();


#ifndef __APPLE__
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
    if(!orig_sigaction)orig_sigaction =  (int (*)()) dlsym(RTLD_NEXT, "sigaction");
	if(signum == SIGABRT || signum == SIGSEGV || signum == SIGILL) return 0; // we def don't want these to be handled in the child
 
    return orig_sigaction(signum,act,oldact);
}
#endif
    
#ifdef __APPLE__
int my_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
   
	if (signum == SIGABRT || signum == SIGSEGV || signum == SIGILL) return 0; // we def don't want these to be handled in the child
    return sigaction(signum,act,oldact);    
}

//
// dyld interposing
//

typedef struct interposer {
  void* replacement;
  void* original;
} interpose_t;

__attribute__((used)) static const interpose_t interposers[]
  __attribute__((section("__DATA, __interpose"))) =
    {
        
      { .replacement = (void*)my_sigaction,
        .original    = (void*)sigaction
      }
    };
 

#endif
