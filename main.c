#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#define MAX_CMD_LENGTH 2048
#define MAX_ARGS 512

void promptUser(char* line, size_t lineSize) {
	printf(": ");
	fflush(stdout);
	getline(&line, &lineSize, stdin);
}

void expandPid(char* str) {
	char* ptr;
	char buf[MAX_CMD_LENGTH];

	while ((ptr = strstr(str, "$$"))) {
		*ptr = '\0';
		sprintf(buf, "%s%d%s", str, getpid(), ptr + 2);
		strcpy(str, buf);
	}
}


//void executeCommand(char** args, int argCount, int* status, int* background) {
//	int childStatus;
//	pid_t spawnPid = -5;
//
//	if (strcmp(args[0], "exit") == 0) {
//		exit(0);
//	} else if (strcmp(args[0], "cd") == 0) {
//		if (argCount > 1) {
//			chdir(args[1]);
//		} else {
//			chdir(getenv("HOME"));
//		}
//		*status = 0;
//	} else if (strcmp(args[0], "status") == 0) {
//		printf("exit value %d\n", *status);
//		*status = 0;
//	} else {
//		spawnPid = fork();
//		switch (spawnPid) {
//			case -1:
//				perror("fork() failed!");
//				exit(1);
//				break;
//			case 0:
//				// child process
//				if (*background == 1) {
//					freopen("/dev/null", "w", stdout);
//					freopen("/dev/null", "w", stderr);
//				}
//
//				if (execvp(args[0], args) < 0) {
//					printf("%s: no such file or directory\n", args[0]);
//					fflush(stdout);
//					exit(1);
//				}
//				break;
//			default:
//				// parent process
//				if (*background == 0) {
//					spawnPid = waitpid(spawnPid, &childStatus, 0);
//					if (WIFEXITED(childStatus)) {
//						*status = WEXITSTATUS(childStatus);
//					} else if (WIFSIGNALED(childStatus)) {
//						printf("terminated by signal %d\n", WTERMSIG(childStatus));
//						fflush(stdout);
//						*status = 1;
//					}
//				} else {
//					printf("background pid is %d\n", spawnPid);
//					fflush(stdout);
//				}
//				break;
//		}
//	}
//}

void runStatus(int lastStatus) {
	if (WIFEXITED(lastStatus))
		printf("exit value %d\n", WEXITSTATUS(lastStatus));
	else
		printf("terminated by signal %d\n", WTERMSIG(lastStatus));
}

int main(int argc, const char * argv[]) {
	char line[MAX_CMD_LENGTH];
	
	while(1) {
		promptUser(line, MAX_CMD_LENGTH);

		// Remove newline character from end of input
		line[strcspn(line, "\n")] = '\0';
		if (strlen(line) == 0 || line[0] == '#')
			continue;
		
		// Expand $$ variable
		expandPid(line);
		
		char* token = strtok(line, " ");
		char* args[MAX_ARGS + 1] = { NULL };
		int argCount = 0;
//		int background = 0;
		int lastStatus = 0;
//		char* inputFile = NULL;
//		char* outputFile = NULL;
		
		while (token != NULL && argCount < MAX_ARGS) {
//			if (strcmp(token, "<") == 0) {
//				token = strtok(NULL, " ");
//				inputFile = token;
//			} else if (strcmp(token, ">") == 0) {
//				token = strtok(NULL, " ");
//				outputFile = token;
//			} else if (strcmp(token, "&") == 0) {
//				background = 1;
//				break;
//			} else {
				args[argCount++] = token;
//			}

			token = strtok(NULL, " ");
		}

		if (argCount == 0)
			continue;
		
		if (strcmp(args[0], "exit") == 0) {
			exit(0);
		} else if (strcmp(args[0], "cd") == 0) {
			if (argCount > 1)
				chdir(args[1]);
			else
				chdir(getenv("HOME"));
		} else if (strcmp(args[0], "status") == 0) {
			runStatus(lastStatus);
		}
		
//		executeCommand(args, argCount, &status, &background);
	}
	
	return 0;
}
