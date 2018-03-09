all: crashdog disable_sigaction.so
crashdog: 
	gcc  crashdog.c -o crashdog 
disable_sigaction.so:
	gcc disable_sigaction.c -o disable_sigaction.so -shared -fPIC

clean:
	rm crashdog disable_sigaction.so
