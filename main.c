#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

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

void runStatus(int lastStatus) {
	if (WIFEXITED(lastStatus))
		printf("exit value %d\n", WEXITSTATUS(lastStatus));
	else
		printf("terminated by signal %d\n", WTERMSIG(lastStatus));
}

void executeCommand(char** args, int argCount, int* lastStatus, char* inputFile, char* outputFile, int background) {
	// Fork for new process
	pid_t pid = fork();
	
	if (pid == -1) {
  		perror("fork() failed!");
  		exit(1);
	} else if (pid == 0) {
		// Child process
		
		// Redirect input
		if (inputFile != NULL) {
			int iFd = open(inputFile, O_RDONLY);
			if (iFd == -1) {
				fprintf(stderr, "cannot open %s for input\n", inputFile);
				fflush(stdout);
				exit(1);
			}
			dup2(iFd, 0);
			close(iFd);
		}

		// Redirect output
		if (outputFile != NULL) {
			int oFd = open(outputFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
			if (oFd == -1) {
				fprintf(stderr, "cannot open %s for output\n", outputFile);
				fflush(stdout);
				exit(1);
			}
			dup2(oFd, 1);
			close(oFd);
		}
		
		if (execvp(args[0], args) < 0) {
			fprintf(stderr, "%s: no such file or directory\n", args[0]);
			fflush(stdout);
			exit(1);
		}
		// exec only returns if there is an error
		perror("execvp");
		exit(2);
	} else {
		// In the parent process
//		pid = waitpid(pid, lastStatus, 0);
		
		if (!background) {
			// Wait for child process to complete
			pid = waitpid(pid, lastStatus, 0);
		} else {
			// Print background process ID
			printf("background pid is %d\n", pid);
			fflush(stdout);
		}
	}
	
	// Reset file descriptors
	if (inputFile != NULL)
		dup2(0, 0);

	if (outputFile != NULL)
		dup2(1, 1);
	
	if (background && WIFEXITED(*lastStatus)) {
		// Print background process exit status
		printf("background pid %d is done: ", pid);
		runStatus(*lastStatus);
		fflush(stdout);
	}
}

int main(int argc, const char * argv[]) {
	char line[MAX_CMD_LENGTH];
	pid_t bgPids[200];
	int numBgPids = 0;
	
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
		int lastStatus = 0;
		int background = 0;
		char* inputFile = NULL;
		char* outputFile = NULL;
		
		// Tokenize input
		while (token != NULL && argCount < MAX_ARGS) {
			if (strcmp(token, "<") == 0) {
				inputFile = strtok(NULL, " ");
			} else if (strcmp(token, ">") == 0) {
				outputFile = strtok(NULL, " ");
			} else if (strcmp(token, "&") == 0) {
				background = 1;
				break;
			} else {
				args[argCount++] = token;
			}

			token = strtok(NULL, " ");
		}
		
		if (argCount == 0)
			continue;
		
		// Handle built-in commands
		if (strcmp(args[0], "exit") == 0) {
			for (int i = 0; i < numBgPids; i++)
				kill(bgPids[i], SIGKILL);
//			checkbgpids(bgpids, &numbgpids, &status, true);
//			When this command is run, your shell must kill any other processes or jobs that your shell has started before it terminates itself.
			exit(0);
		} else if (strcmp(args[0], "cd") == 0) {
			if (argCount > 1) {
				if (chdir(args[1])) {
					fprintf(stderr, "Could not find %s\n", args[1]);
					fflush(stdout);
				}
			} else {
				chdir(getenv("HOME"));
			}
		} else if (strcmp(args[0], "status") == 0) {
			runStatus(lastStatus);
		} else {
			executeCommand(args, argCount, &lastStatus, inputFile, outputFile, background);
			
//			if (!background) {
//				pid_t pid = waitpid(-1, &lastStatus, 0);
//				if (pid == -1) {
//					perror("waitpid");
//					exit(1);
//				}
//				runStatus(lastStatus);
//			}
		}
	}
	return 0;
}
