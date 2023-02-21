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
pid_t bgPids[200];
int numBgPids = 0;
int lastStatus = 0;

struct command {
	char *args[MAX_ARGS + 1];
	int argc;
	char *inFile;
	char *outFile;
	int bg;
};

int parseCmd(char* line, struct command* cmd) {
	printf(": ");
	fflush(stdout);
	size_t bufsize = MAX_CMD_LENGTH;
	getline(&line, &bufsize, stdin);

	// Continue if empty or comment
	if (line[0] == '\n' || line[0] == '#')
		return 1;
	
	// Remove newline and & character from end of input
	line[strcspn(line, "\n")] = '\0';
	if (line[strlen(line) - 1] == '&') {
		line[strlen(line) - 1] = '\0';
		cmd->bg = 1;
	}
	
	// Expand PID
	char buf[MAX_CMD_LENGTH];
	for (char* ptr; (ptr = strstr(line, "$$")); ) {
		*ptr = '\0';
		sprintf(buf, "%s%d%s", line, getpid(), ptr + 2);
		strcpy(line, buf);
	}
	
	// Tokenize
	char* token = strtok(line, " ");
	while (token != NULL && cmd->argc < MAX_ARGS) {
		if (!strcmp(token, "<"))
			cmd->inFile = strtok(NULL, " ");
		else if (!strcmp(token, ">"))
			cmd->outFile = strtok(NULL, " ");
		else
			cmd->args[cmd->argc++] = token;
		token = strtok(NULL, " ");
	}
	return 0;
}

void runCd(struct command cmd) {
	if (cmd.argc > 1) {
		if (chdir(cmd.args[1])) {
			fprintf(stderr, "Could not find %s\n", cmd.args[1]);
			fflush(stdout);
		}
	} else
		chdir(getenv("HOME"));
}

void runStatus(int lastStatus) {
	if (WIFEXITED(lastStatus))
		printf("exit value %d\n", WEXITSTATUS(lastStatus));
	else if (WIFSIGNALED(lastStatus))
		printf("terminated by signal %d\n", WTERMSIG(lastStatus));
	fflush(stdout);
}

void checkBg(int* bgPids, int* numBgPids, int* status, int terminate) {
	for (int i = 0; i < *numBgPids; i++) {
		pid_t pid = waitpid(bgPids[i], status, WNOHANG);
		
		if (terminate)
			kill(bgPids[i], SIGKILL);
		
		if (pid != 0) {
			printf("background pid %d is done: ", bgPids[i]);
			fflush(stdout);
			runStatus(*status);
			for (int j = i; j < *numBgPids - 1; j++)
			   bgPids[j] = bgPids[j + 1];
			
			bgPids[*numBgPids] = 0;
			(*numBgPids)--;
		}
	}
	if (terminate)
		exit(0);
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

void newProcess(struct command cmd, struct sigaction sa) {
	// Fork for new process
	pid_t pid = fork();
	
	if (pid == -1) {
		perror("fork() failed!");
		exit(1);
	} else if (pid == 0) {
		// Child process
		if (!cmd.bg) {
			sa.sa_handler = SIG_DFL;
			sa.sa_flags = 0;
			sigaction(SIGINT, &sa, NULL);
		}
		
		redirectIO(cmd.inFile, 0, O_RDONLY, "input");
		redirectIO(cmd.outFile, 1, O_WRONLY | O_CREAT | O_TRUNC, "output");
		execvp(cmd.args[0], cmd.args);
		fprintf(stderr, "%s: no such file or directory\n", cmd.args[0]);
		fflush(stdout);
		exit(1);
	} else {
		// In the parent process
		if (cmd.bg && !fgMode) {
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
	if (cmd.inFile || cmd.outFile)
		dup2(0, 0), dup2(1, 1);
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
	struct sigaction sa = {0}, SIGTSTP_action = {0};
	set_signal_handler(SIGINT, SIG_IGN, 0, &sa);
	set_signal_handler(SIGTSTP, handle_SIGTSTP, SA_RESTART, &sa);
	
	char line[MAX_CMD_LENGTH];
	
	while(1) {
		checkBg(bgPids, &numBgPids, &lastStatus, 0);
		
		// Init command struct
		struct command cmd = {
			.args = { NULL },
			.argc = 0,
			.inFile = NULL,
			.outFile = NULL,
			.bg = 0
		};
		
		// Parse command and skip if error or empty
		if (parseCmd(line, &cmd) || !cmd.argc)
			continue;
		
		// Handle built-in commands
		if (!strcmp(cmd.args[0], "exit"))
			checkBg(bgPids, &numBgPids, &lastStatus, 1);
		else if (!strcmp(cmd.args[0], "cd"))
			runCd(cmd);
		else if (!strcmp(cmd.args[0], "status"))
			runStatus(lastStatus);
		else
			newProcess(cmd, sa);
	}
	return 0;
}
