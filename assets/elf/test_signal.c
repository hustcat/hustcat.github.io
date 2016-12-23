#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>


volatile sig_atomic_t signal_count = 0;


static void sigprof_handler(int sig_nr, siginfo_t* info, void *context)
{
	signal_count++;
	//printf("Recv: %d\n", sig_nr);
}

void install_signal_handler()
{
	/* Install signal handler for SIGPROF event */
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = sigprof_handler;
	sa.sa_flags = SA_RESTART | SA_SIGINFO;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGPROF, &sa, NULL);
}


int main(void)
{
	fd_set rfds;
	struct timeval tv;
	int retval;
	int ret = 0;
	int i;

	install_signal_handler();
	static struct itimerval timer;

	timer.it_interval.tv_sec = 1;
	timer.it_interval.tv_usec = 0;
	timer.it_value = timer.it_interval;


	/* Install timer */
	if (setitimer(ITIMER_PROF, &timer, NULL) != 0)
	{
		printf("Timer could not be initialized \n");
	}

	for(i =0; i< 1024 * 2; i++){
		char *p = (char*)malloc(1024*1024);
		memset(p, 0, 1024*1024);
	}
	while(1){
		
	}
	exit(EXIT_SUCCESS);
}
