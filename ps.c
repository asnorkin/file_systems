#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


#define PROC "/proc/"
#define EXE "/exe"
#define COMM "/comm"
#define INDENT 10


ssize_t read_exe(const char *pid, char *cmd, ssize_t len)
{
    char exe_path[1024];
    sprintf(exe_path, "%s%s%s", PROC, pid, EXE);

    return readlink(exe_path, cmd, len);
}

ssize_t read_comm(const char *pid, char *cmd)
{
    char comm_path[1024];
    sprintf(comm_path, "%s%s%s", PROC, pid, COMM);

    FILE *comm = fopen(comm_path, "r");
    if(!comm) {
        return -1;
    }
    fscanf(comm, "%s", cmd);
    fclose(comm);

    return strlen(cmd);
}

int main(void)
{
    DIR *proc_dir = opendir(PROC);
    if(!proc_dir) {
        perror("Can't open \"/proc\"");
        exit(-1);
    }

    int width1 = INDENT, width2 = INDENT;
    printf("%-*s%-*s\n", width1, "PID", width2, "CMD");

    while(1)
    {
        struct dirent *curr_dir = readdir(proc_dir);
        if(!curr_dir)
            break;

        int curr_PID = atoi(curr_dir->d_name);
        if(!curr_PID)
            continue;


        char cmd[1024];
        ssize_t cmd_len = read_exe(curr_dir->d_name, cmd, sizeof(cmd)-1);
        if(cmd_len == -1) {
            cmd_len = read_comm(curr_dir->d_name, cmd);
            if(cmd_len == -1) {
                perror(curr_dir->d_name);
                exit(-1);
            }
        }

        cmd[cmd_len] = '\0';
        printf("%-*d%-*s\n", width1, curr_PID, width2, cmd);
    }

    return 0;
}

