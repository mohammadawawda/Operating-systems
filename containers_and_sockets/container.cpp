#include <iostream>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define STACK_SIZE 8192

using namespace std;

/**
 * command line arguments for child process
 * */
struct child_params{
    char* hostname;
    char* fs_dir;
    unsigned int num_proc;
    char* path_to_run;
    char* args;
};

/**
 * uses errno for error identification
 * */
void sys_error_handler(const string& msg){
    cerr << "system error: " << msg << " -> " << strerror(errno) << endl;
    exit(1);
}

/**
 * code for child process
 * set hostname
 * change root directory
 * change working dir
 * mount proc fs
 * execute code given
 *
 * limit on number of processes is not implemented
 * */
int container(void* params){
    auto cp = static_cast<child_params *>(params);
    // set hostname
    if (sethostname(cp->hostname, strlen(cp->hostname)) != 0){
        sys_error_handler("set hostname");
    }

    // change to root dir of new filesystem
    if (chroot(cp->fs_dir) != 0){
        sys_error_handler("chroot FS");
    }


    // change work dir
    if (chdir("/") != 0){
        sys_error_handler("chdir FS");
    }

    // mount proc filesystem
    if (mount("proc", "/proc", "proc", 0, 0) != 0){
        sys_error_handler("mount proc");
    }

    char* _args[] = {cp->path_to_run, cp->args, (char* )0};
    if (execvp(cp->path_to_run, _args) == -1){
      sys_error_handler("execvp");
    }

    return 0;
}

/**
 * father operations to create child process, and when it is finished
 * propagates error exit codes from child
 * */
void create_container(char* hostname,  char* fs_dir, unsigned int num_proc, char* path_to_run,
                      char* args_for_program){

    // allocate stack
    char * stack = (char *) malloc(STACK_SIZE);
    if  (stack == nullptr){
        sys_error_handler("mem alloc");
    }

    child_params params = {hostname, fs_dir, num_proc, path_to_run, args_for_program};

    int child_pid = clone(container, stack + STACK_SIZE - 1 ,
                           SIGCHLD | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS,
                           (void *) &params);

    int status;
    if (wait(&status) == -1){
        sys_error_handler("wait");
    }

    else {
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 1) {
                exit(1);
            }
        }

        if (child_pid == -1) {
            sys_error_handler("clone");
        }

        // unmount proc
        if (umount( strcat(fs_dir, "/proc")) != 0) {
            sys_error_handler("umount proc");
        }

        free(stack);
    }

}



int main(int argc, char* argv[]){

    create_container(argv[1], argv[2], atoi(argv[3]),
                     argv[4], argv[5]);

    return 0;
}
