# Crashdog
A small utility used to throw a directory of testcases agaist a single target, keeping the crashing ones. 
Borrows heavily from AFL, keeping most of the optrions similar. 

```
Example usage: ./crashdog -i <input dir> -o <output dir> -m MEMORY_LIMIT -t TIMEOUT -- ./target args @@
Options:
	-i - input directory containing testcases - required
	-o - output directory to save crashes in -required
	-m - memory limit (in MB) - optional
	-t - time limit (in seconds, keep high) - optional 
	-l - log file, otherwise target stdin/err goes to /dev/null - optional
	-d - path to library to set as LD_PRELOAD (libduma, libdislocator...) - optional
	-p - post-exexecution command, for cleanup after each testcase (be careful) - optional
	-f - create symlink in certain place for current testcase - optional
	-h - prints this help


```

It has a "nice" stats screen too:

```
Command line: /usr/bin/file @@ 
Testcases directory: /home/testing/inputs
Output directory: testout
Total number of testcases: 4646
Testcases executed so far: 312
Crashes so far: 0
Timeouts so far: 8
Current testcase: fc28007c956f010ac5831a72f58f0dcd95ecb65d
Avg. speed: 37.94 execs per second
Total time: 0 days 00:00:27
[======----------------------------------------------------------------------------------------------] 6%
```

That being said, the stats in the stats screen are calculated very crudely and are to be used just as a hint that everything
is doing ok or not.

### Usage

The tool tries to be smart about couple of things. It will try to detect the type of binary for some sanity checks.
ASAN binaries are detected automatically and appropriate environment variables are set so crashes get caught. If an 
AFL or afl-dyninst binary is detected, warning is shown to the user, since in most cases we don't want to run against
those binaries. 

Using the `-d` option, a list of LD_PRELOAD libraries can be specified. Main use of this option is to load libdislocator
or libduma allocators so we can detect more memory issues without loosing too much speed. Also, this can be used
to override/disable certain lib functions. The tool comes with `disable_sigaction` shared lib which is intended to
be used against uncooperative targets that set their own signal handlers for SIGABRT or SIGSEGV.
More than one library can be specified by surrounding them with quotes and delimiting with a space (like LD_PRELOAD  usually works).

Time limit option is mean to be used as a guard against infinite loops or otherwise application locking bugs in the target.
It's not intended for skipping "slow" inputs, so keep it reasonably high. 

Memory limit can be used to set upper limit for virtual memory available to the target. This can be usefull for some application
that don't behave nicely, but can otherwise be left unused. 

Option `-p`, or post cleanup option, is intended to be a small command which gets passed directly to `system()` and is executed after each
testcase. Some targets will generate a lot of junk which can quickly fill up disk space, so it's usually used to remove those. Commands
can be wrapped in quotes, just be carefull what you do. 

Some targets don't use command line arguments to specify the target file, but some other mechanism, option `-f` allows you to keep the same
file name as a target , which gets symlinked to different testcases. 

A comperhensive command line for running this tool might look something like this:

```
 ./crashdog -m 100 -t 60  -l 1.log -i ../inputs -o ./testout/ -d "../../afl/libdislocator.so" -p "rm *.html" -- pdf2html @@
``` 



Feature requests and bug reports welcome!