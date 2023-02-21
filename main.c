#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_CMD_LENGTH 2048
#define MAX_ARGS 512

int fgMode = 0;

struct command {
	char *cmd;
	char *args[MAX_ARGS];
	char *builtArgs[MAX_ARGS];
	char *inputFile;
	char *outputFile;
	int background;
	int redirect;
};

void promptUser(char* line, size_t lineSize) {
	printf(": ");
	fflush(stdout);
	getline(&line, &lineSize, stdin);
	
	// Remove newline character from end of input
	line[strcspn(line, "\n")] = '\0';
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
	else if (WIFSIGNALED(lastStatus))
		printf("terminated by signal %d\n", WTERMSIG(lastStatus));
	fflush(stdout);
}

void redirectIO(char* file, int redirFd, int flags, char* action) {
	if (file != NULL) {
		int fd = open(file, flags, 0644);
		if (fd == -1) {
			fprintf(stderr, "cannot open %s for %s\n", file, action);
			fflush(stdout);
			exit(1);
		}
		dup2(fd, redirFd);
		close(fd);
	}
}

void checkBg(int* bgPids, int* numBgPids, int* status, int terminate) {
	for (int i = 0; i < *numBgPids; i++) {
		pid_t pid = waitpid(bgPids[i], status, WNOHANG);
		
		if (terminate)
			kill(bgPids[i], SIGKILL);
		
		if (pid != 0) {
			printf("\nbackground pid %d is done: ", bgPids[i]);
			fflush(stdout);
			runStatus(*status);
			for (int j = i; j < *numBgPids - 1; j++)
			   bgPids[j] = bgPids[j + 1];
			
			bgPids[*numBgPids] = 0;
			(*numBgPids)--;
		}
	}
}
void set_signal_handler(int signum, void (*handler)(int), int flags, struct sigaction *sa) {
	sa->sa_handler = handler;
	sigfillset(&sa->sa_mask);
	sa->sa_flags = flags;
	sigaction(signum, sa, NULL);
}

void handle_SIGTSTP(int signo) {
	char* message = fgMode ? "\nExiting foreground-only mode\n: " : "\nEntering foreground-only mode (& is now ignored)\n: ";
	fgMode = !fgMode;
	write(STDOUT_FILENO, message, strlen(message));
	fflush(stdout);
}

int main(int argc, const char * argv[]) {
	struct sigaction SIGINT_action = {0}, SIGTSTP_action = {0};
	set_signal_handler(SIGINT, SIG_IGN, 0, &SIGINT_action);
	set_signal_handler(SIGTSTP, handle_SIGTSTP, SA_RESTART, &SIGTSTP_action);
	
	char line[MAX_CMD_LENGTH];
	pid_t bgPids[200];
	int numBgPids = 0;
	int lastStatus = 0;
	
	while(1) {
		checkBg(bgPids, &numBgPids, &lastStatus, 0);
		promptUser(line, MAX_CMD_LENGTH);
		if (strlen(line) == 0 || line[0] == '#')
			continue;
		
		// Expand $$ variable
		expandPid(line);
		
		// Init command vars
		int argCount = 0;
		char* args[MAX_ARGS + 1] = { NULL };
		char* inputFile = NULL;
		char* outputFile = NULL;
		
		char* token = strtok(line, " ");
		// Tokenize input
		while (token != NULL && argCount < MAX_ARGS) {
			if (!strcmp(token, "<"))
				inputFile = strtok(NULL, " ");
			else if (!strcmp(token, ">"))
				outputFile = strtok(NULL, " ");
			else
				args[argCount++] = token;
			token = strtok(NULL, " ");
		}
		
		int background = 0;
		if (argCount > 0 && !strcmp(args[argCount - 1], "&")) {
			background = 1;
			args[argCount - 1] = NULL;
			argCount--;
		}
		
		if (!argCount)
			continue;
		
		// Handle built-in commands
		if (!strcmp(args[0], "exit")) {
			checkBg(bgPids, &numBgPids, &lastStatus, 1);
			exit(0);
		} else if (!strcmp(args[0], "cd")) {
			if (argCount > 1) {
				if (chdir(args[1])) {
					fprintf(stderr, "Could not find %s\n", args[1]);
					fflush(stdout);
				}
			} else {
				chdir(getenv("HOME"));
			}
		} else if (!strcmp(args[0], "status")) {
			runStatus(lastStatus);
		} else {
			// Fork for new process
			pid_t pid = fork();
			
			if (pid == -1) {
				perror("fork() failed!");
				exit(1);
			} else if (pid == 0) {
				// Child process
				if (!background) {
					SIGINT_action.sa_handler = SIG_DFL;
					SIGINT_action.sa_flags = 0;
					sigaction(SIGINT, &SIGINT_action, NULL);
				}
				
				redirectIO(inputFile, 0, O_RDONLY, "input");
				redirectIO(outputFile, 1, O_WRONLY | O_CREAT | O_TRUNC, "output");
				execvp(args[0], args);
				fprintf(stderr, "%s: no such file or directory\n", args[0]);
				fflush(stdout);
				exit(1);
			} else {
				// In the parent process
				if (background && !fgMode) {
					// Print background process ID
					bgPids[numBgPids++] = pid;
					printf("background pid is %d\n", pid);
					fflush(stdout);
				} else {
					// Wait for child process to complete
					waitpid(pid, &lastStatus, 0);
				}
			}
			
			// Reset file descriptors
			if (inputFile != NULL)
				dup2(0, 0);

			if (outputFile != NULL)
				dup2(1, 1);
		}
	}
	return 0;
}
