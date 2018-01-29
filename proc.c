#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <dirent.h>


/*int main() {

	DIR* directory = NULL;
	DIR* proc_directory = NULL;

	struct dirent* dir_contence = NULL;
	struct dirent* proc_contence = NULL;
	char exec[5] = "exe";

	directory = opendir("/proc");

	if(directory == NULL) {
		perror("Couldn't open '/proc'\n");
		return errno;
	}

	do {
		errno = 0;
		if((dir_contence = readdir(directory)) != NULL) {
			int PID;
			PID = atoi(dir_contence -> d_name);
			if(PID == 0) continue;
																						printf("Proc: %d\n", PID);
				


			//Process internals
				char dest[100] = "/proc/";
				strcat(dest, dir_contence -> d_name);
				proc_directory = opendir(dest);
				if(proc_directory == NULL) {
					perror("Couldn't open '/proc/[PID]'\n");
					return errno;
				}
				strcat(dest, "/exe");
				do {
					errno = 0;
					if((proc_contence = readdir(proc_directory)) != NULL) {
						if(strcmp(proc_contence -> d_name, exec) != 0) continue;
																						printf("	exe is found!\n");
						char buf[1024];
						ssize_t result = readlink(dest, buf, 1023);
																						printf("%zd\n", result);
						if(result == -1) {
							perror("Problem with link!\n");
							return errno;
						}
                        //buf[result] = '\0';
                                                                                        //printf("		%s\n, buf");
					}
				} while (proc_contence != NULL);
				(void)closedir(proc_directory);
				proc_directory = NULL;
				proc_contence = NULL;



		}
	} while (dir_contence != NULL);

	(void)closedir(directory);
	return 0;
}*/
