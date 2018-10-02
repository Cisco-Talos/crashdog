#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/mman.h>

#ifdef __APPLE__
#include <copyfile.h>
#include <mach-o/loader.h>
#else
#include <sys/sendfile.h>
#endif



//options
static char *outdir;
static char *indir;
static char *log_fname;
static char *post_command;
static char *ld_preload;
static char *target_file;
static unsigned int memory_limit;
static unsigned int time_limit;

//status updates
static unsigned int update_stats_after;
static unsigned int total_files;
static unsigned int executed_so_far;
static unsigned int total_crashes;
static unsigned int total_timeouts;
static time_t begin_time;
static unsigned long long  int total_usecs;
static int afl_binary = 0;
static int afl_dyninst_binary = 0;
static int asan_binary = 0;
double avg_speed;
static int update_needed;
char *current_testcase;
char *command_line;

char *executable;

// misc
int testcase_timed_out;  
pid_t pid;

void print_time(){
	unsigned int diff = (unsigned int)difftime(time(NULL),begin_time);
	int days,hours,minutes,seconds;
	days = diff / (24*3600);
	diff -= days*(24*3600);
	hours = diff/3600;
	diff -= hours*3600;
	minutes = diff/60;
	diff -= minutes*60;
	seconds = diff;
	printf("Total time: %d days %02d:%02d:%02d\n",days,hours,minutes,seconds);

}

//imprecise
void print_progress_bar(){
	unsigned int segments = 100;
	unsigned int segments_done;
	char progress_bar[segments+10];
	char percent_str[5];
	int i;
	segments_done =  executed_so_far / (total_files / (double)segments);
	progress_bar[0] = '[';
	for(i = 1; i <= segments_done; i++) progress_bar[i] = '=';
	for(i = segments_done+1;i<=segments; i++) progress_bar[i] = '-';
	progress_bar[segments+1] = ']';
	sprintf(percent_str," %d%%",segments_done);
	strcat(progress_bar,percent_str);
	printf("%s\n",progress_bar);
	
}

void display_stats(){
	system("clear");
	printf("\n\n");
	printf("Command line: %s\n",command_line );
	printf("Testcases directory: %s\n",indir);
	printf("Output directory: %s\n",outdir);
	printf("Total number of testcases: %d\n",total_files);
	printf("Testcases executed so far: %d\n",executed_so_far);
	printf("Crashes so far: %d\n",total_crashes);
	printf("Timeouts so far: %d\n", total_timeouts);
	printf("Current testcase: %s\n",current_testcase);
	if(executed_so_far > 10){
	unsigned int avg_time = total_usecs/executed_so_far;
	if(avg_time == 0) avg_time =1;
	avg_speed = 1000000.0 / avg_time;
	printf("Avg. speed: %.2f execs per second\n",avg_speed); // this is realy imprecise , just a hint 
	print_time();
	print_progress_bar();
	
	}
	if(afl_binary)printf("Warning: testing against AFL instrumented binary!\n");
	if(afl_dyninst_binary)printf("Warning: testing against afl-dyninst instrumented binary!\n\n");
	if(asan_binary) printf("Triaging against AddressSanitizer binary. ASAN env vars enabled.\n");
	#ifdef __APPLE__
		char *p = getenv("DYLD_INSERT_LIBRARIES"); 
		if(!p || strcmp(p, "/usr/lib/libgmalloc.dylib")) {printf("Warning: Consider running with libgmalloc (see man libgmalloc)\n");}
	#else
		if(!ld_preload && !asan_binary) {printf("Warning: Consider running with libdislocator or libduma to catch more crashes (option -d)!\n");}
	#endif
	printf("\n\n");

}

void usage(){
	printf("Example usage: ./crashdog -i <input dir> -o <output dir> -m MEMORY_LIMIT -t TIMEOUT -- ./target args @@\n");
	printf("Options:\n");
	printf("\t-i - input directory containing testcases - required\n");
	printf("\t-o - output directory to save crashes in -required\n");
	printf("\t-m - memory limit (in MB) - optional\n");
	printf("\t-t - time limit (in seconds, keep high) - optional \n");
	printf("\t-l - log file, otherwise target stdin/err goes to /dev/null - optional\n");
	printf("\t-d - path to library to set as LD_PRELOAD (libduma, libdislocator...) - optional\n");
	printf("\t-p - post-exexecution command, for cleanup after each testcase (be careful) - optional\n");
	printf("\t-f - create symlink in certain place for current testcase - optional\n");
	printf("\t-h - prints this help\n");
	exit(EXIT_SUCCESS);
}

void testcase_timeout_handler(int sig){
	testcase_timed_out = 1;
	if(pid > 0) kill(pid,SIGKILL);
	total_timeouts++;
	update_needed = 1;
}


void setup_handlers(){

  struct sigaction sa;

  sa.sa_handler   = NULL;
  sa.sa_flags     = SA_RESTART;
  sa.sa_sigaction = NULL;
  sigemptyset(&sa.sa_mask);
  sa.sa_handler = testcase_timeout_handler;
  sigaction(SIGALRM, &sa, NULL);

}

void copy_testcase(char *current_testcase){

	char *src_file;
	char *dst_file;
	int src_fd;
	int dst_fd;
	off_t file_offset = 0;
	struct stat finfo = {0};

	src_file = (char *)malloc(strlen(indir)+strlen(current_testcase)+2);
	dst_file = (char *)malloc(strlen(outdir)+strlen(current_testcase)+2);
	sprintf(src_file,"%s/%s",indir,current_testcase);
	sprintf(dst_file,"%s/%s",outdir,current_testcase);
    

#ifdef __APPLE__ 
    copyfile(src_file, dst_file, NULL, COPYFILE_ALL); // COPYFILE_DATA
#else       
	if((src_fd = open(src_file,O_RDONLY)) == -1){
		perror("open");
		exit(EXIT_FAILURE);
	}
	if((dst_fd = open(dst_file,O_RDWR|O_CREAT,0666)) == -1){
		perror("open");
		exit(EXIT_FAILURE);
	}
	fstat(src_fd,&finfo);
	
    sendfile(dst_fd,src_fd,&file_offset,finfo.st_size);
    close(src_fd);
	close(dst_fd);    
#endif    
    

	free(src_file);
	free(dst_file);
}

void create_testcase_symlink(char* indir, char* current_testcase)
{
		char* testcase_full_path = 0;
		int abs_path_len = strlen(indir) + strlen(current_testcase) + 2;
		testcase_full_path = (char *)malloc(abs_path_len);
		snprintf(testcase_full_path,abs_path_len,"%s/%s",indir,current_testcase);
		//delete old file/symlink 
		unlink(target_file);
		//link with testcase
		symlink(testcase_full_path,target_file);

		if(testcase_full_path)
			free(testcase_full_path);
}

void triage(char **argv){

    DIR *dir;
	struct dirent *ent;
	char *cwd = getcwd(NULL,0);
	char *input_arg; 
	int i = 0;
	int status;
	static struct itimerval it;

	if(!cwd){
		perror("getcwd failed");
		exit(EXIT_FAILURE);
	}
	
	//list input directory
	if ((dir =  opendir(indir)) == NULL ){
		perror("Input directory");	
		exit(EXIT_FAILURE);
	}	
	
	while((ent = readdir(dir)) != NULL){
		if(ent->d_type != DT_REG) continue;
		current_testcase = ent->d_name;

		if((executed_so_far % update_stats_after) == 0 || update_needed ){
			 display_stats();
			 update_needed = 0;
		}
		
		pid = fork();
		if(pid < 0){
			perror("fork");
			exit(EXIT_FAILURE);
		}

		if(pid == 0){ // begin child 
			input_arg = NULL;
			while(argv[i]){
				//adjust argv , substitute @@ with path to sample
				if(strstr(argv[i],"@@")){
					// full sample path is indir/current_testcase			
					int abs_path_len = strlen(indir) + strlen(current_testcase) + 2;
					input_arg = (char *)malloc(abs_path_len);
					snprintf(input_arg,abs_path_len,"%s/%s",indir,current_testcase);
					argv[i] = input_arg;
					break;
				}
				i++;
			}
			//create target file if needed
			if(target_file)
				create_testcase_symlink(indir,current_testcase);
		
			//set rlimits
			struct rlimit r;
			if(memory_limit){
	        	r.rlim_max = r.rlim_cur = ((rlim_t)memory_limit); // it's givven in MB
				setrlimit(RLIMIT_AS,&r);

			}
			r.rlim_max = r.rlim_cur = 0;
			setrlimit(RLIMIT_CORE,&r);

			//make it shut up or redirect stdout to file
			int fd_null = open(log_fname,O_RDWR|O_CREAT|O_APPEND,0666);
			if(fd_null < 0){
				 perror("open:");
				exit(EXIT_FAILURE);
			}
			dup2(fd_null,1);
			dup2(fd_null,2);
			close(fd_null);
			
			if(asan_binary){
				//set asan options so we catch ASAN crashes too
				setenv("ASAN_OPTIONS",  "abort_on_error=1:"
                           				"detect_leaks=0:"
                           				"symbolize=0:"
                           				"allocator_may_return_null=1", 1);
			}

			if(ld_preload){
#ifdef __APPLE__                
				setenv("DYLD_INSERT_LIBRARIES", ld_preload, 1);  
#else
				  
                setenv("LD_PRELOAD",ld_preload,1);            
#endif
                
                
			}
			//finally, run the testcase
			execv(executable,argv);
	
			exit(EXIT_SUCCESS);
		} // end child
		
		if(time_limit){
			testcase_timed_out = 0;
			it.it_value.tv_sec = time_limit;	
		}
		setitimer(ITIMER_REAL,&it,NULL);
		struct timeval start, stop;
		gettimeofday(&start,NULL);


		if (waitpid(pid, &status, 0) <= 0){
			perror("waitpid");
			exit(EXIT_FAILURE);
		}

		gettimeofday(&stop,NULL);
		if(stop.tv_usec - start.tv_usec > 0)
		total_usecs += (stop.tv_usec - start.tv_usec);	

		it.it_value.tv_sec = 0;
		setitimer(ITIMER_REAL, &it, NULL);

		if(WIFSIGNALED(status) && testcase_timed_out != 1 ){
			// we have a crash!
			// copy the file 
			copy_testcase(current_testcase);	
			total_crashes++;
			update_needed = 1;
		}

		executed_so_far++;

		if(post_command)system(post_command);
	
	}
	
	free(cwd);
	display_stats();
	printf("DONE!\n");
}

unsigned int count_files(char *dirpath){
	struct dirent *df;
	DIR *fd;
	unsigned int count = 0;
	if((fd = opendir(dirpath))== NULL){
		perror("directory");
		exit(EXIT_FAILURE);
	}
	while((df = readdir(fd)) != NULL){
		if(df->d_type == DT_REG) count++;
	}

	return count;
}

//concats cmd args for stats display
void set_command_line(char **argv){
	unsigned int cmdline_len = 0;
	int i;
	for(i = 0;argv[i]; i++) cmdline_len += strlen(argv[i]) + 1;
	command_line = (char *) malloc(cmdline_len+1);
	memset(command_line,'\0',cmdline_len);
	for(i = 0; argv[i]; i++) {
		strcat(command_line, argv[i]);
		strcat(command_line, " ");
	}
}

// by default, distros will have crashes info sent to some utility, which slows things down
// look up core_pattern and warn user
void check_core_pattern(){
	int proc_fd = open("/proc/sys/kernel/core_pattern",O_RDONLY);
	char c;

	if(proc_fd < 0){
		printf("cannot open core pattern, is this linux?\n");
	}else{
		if(read(proc_fd, &c, 1) == 1 && c == '|'){
			printf("System is configured to pipe coredump notifications to external utility.\n");
			printf("This can cause delays, leading to timeouts and false negatives.\n");
			printf("We don't want this, execute (as root):\n");
			printf("\techo core >/proc/sys/kernel/core_pattern\n");
			exit(EXIT_FAILURE);
		}
	}
	
}


// various checks against the target binary
// try to find it in cwd, then in path
// check if it's elf at all
// check asan, afl or dyninst and set flags accordingly
void check_target_binary(char **args){
	//see if the executable exists
	//if not, try in $PATH
	
	char *env_path,*path;
	if(access(args[0],X_OK)){ //binary not found
		env_path = getenv("PATH");
		if(env_path){
			char *start = env_path;
			char *end;
			while((end = strchr(start,':')) != NULL){
				path = (char *)malloc(end-start  + strlen(args[0])+3);
				memset(path,'\0',end-start+strlen(args[0])+3);
				memcpy(path, start, end-start);
				strcat(path,"/");
				strcat(path,args[0]);
				if(!access(path,X_OK)){
					executable = path;
					break;				
				}else{
					free(path);
				}
				start = end+1;
			}
			if(!executable){
				printf("Binary not found or not executable!\n");
				exit(EXIT_FAILURE);
			}
		}
	}else{
		executable = args[0];
	}
	
	// is it an ELF at all?
	int fd = open(executable, O_RDONLY);
	struct stat fstats;
	stat(executable,&fstats);
	if(fd < 0){
		perror("opening executable");
		exit(EXIT_FAILURE);
	}


#ifdef __APPLE__
    // check apple data
    #define FAT_MAGIC   0xcafebabe
    #define FAT_CIGAM   0xbebafeca
    
    struct mach_header     mach_hdr         = {0};
    uint32_t               magic_values[]   = { FAT_MAGIC, FAT_CIGAM, MH_MAGIC, MH_CIGAM, MH_MAGIC_64, MH_CIGAM_64, 0 };
    int                    found            = -1;
    
    
    if (read(fd, &mach_hdr, sizeof(mach_hdr)) != sizeof(mach_hdr))
    {
        printf("Unable to read MACH-O header \n");
        close(fd);
        exit(EXIT_FAILURE);
    }
    
    
    for (int i = 0; magic_values[i] != 0; i++)
    {
        if (mach_hdr.magic == magic_values[i])
        {
            found = i;
            if (i < 2) printf("Warning: MACH-O fat header detected \n");            
            break;
        }
    }

    
    if (found < 0)
    {
        printf("Target not MACH-O file. A script maybe? (magic = 0x%08x)? \n", mach_hdr.magic);
        close(fd);
        exit(EXIT_FAILURE);
    }        
    
    printf("Target is MACH-O file (magic = 0x%08x) \n", mach_hdr.magic);   
    close(fd);
    return;
#endif 
    
	//peek first few bytes and confirm ELF header
	unsigned int elf_hdr;
	if(read(fd,&elf_hdr,4) != 4 || elf_hdr != 0x464c457f){
		printf("Target not an ELF file.A script maybe?\n");
		exit(EXIT_FAILURE);
	}
	
	// if it has AFL vars, it's AFL binary
	char *elf_data = mmap(0, fstats.st_size, PROT_READ,MAP_PRIVATE,fd,0);
	// is it AFL compiled
	if(memmem(elf_data, fstats.st_size, "__AFL_SHM_ID",12 )){
		afl_binary = 1;
	}

	// is it dyninst compiled? 
	if(memmem(elf_data, fstats.st_size, "libAflDyninst.so",16)){ // too simple?
		afl_dyninst_binary = 1;
	}

	// is it ASAN compiled
	if(memmem(elf_data, fstats.st_size, "__asan_init",11)){ // too simple?
		asan_binary = 1;
	}
 	
 	munmap(elf_data,fstats.st_size);	
	close(fd);
}

void main(int argc, char **argv){
	
	//parse options
	int opt;
	char **target_args;
	while((opt = getopt(argc,argv,"o:i:m:t:l:p:d:f:h")) > 0){
		
		switch(opt){
			case 'o':
				outdir = optarg;
				break;
			case 'i':
				indir = optarg;
				break;
			case 'm':
				memory_limit = atoi(optarg) * 1024 * 1024;
				break;
			case 't':
				time_limit = atoi(optarg);			
				break;
			case 'l':
				log_fname = optarg;
				break;
			case 'p':
				post_command = optarg;
				break;
			case 'd':
				ld_preload = optarg;
				break;
			case 'f':
				target_file = optarg;
				break;				
			case 'h':
			default:
				usage();	
		}
	}
	if (optind == argc || !outdir || !indir ){
		usage();
	}		
	if(!log_fname) log_fname = "/dev/null";
	target_args = argv + optind;
	
	setup_handlers();
	
	//check and/or create output dir
	DIR *tmp = opendir(outdir);
	if(!tmp && ENOENT == errno){
		mkdir(outdir,0777);
	}else if(tmp){
		closedir(tmp);	
	}else{
		perror("output directory");
		exit(EXIT_FAILURE);
	}
	
	//check core format - 
	check_core_pattern();
	
	//check and/or find target binary
	check_target_binary(target_args);
	
	//get number of testcases
	total_files = count_files(indir);
	
	//record start time
	begin_time = time(NULL);

	//crude update frequency estimation	
	update_stats_after = (total_files/100)*0.1; // update every 0.1% of executions
	if(!update_stats_after) update_stats_after = 5; // too little files, update all the time 		
	
	set_command_line(target_args);
	triage(target_args);

}
