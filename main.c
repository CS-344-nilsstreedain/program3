/**
 * @file main.c
 * @author Nils Streedain (https://github.com/nilsstreedain)
 * @brief An implementation of a shell in C
 *
 * This file contains the implementation of a basic shell in C, which can execute commands and handle basic input/output redirection, as well as background processes.
 */
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

// Specifies if foreground-only mode should be enabled
int fgMode = 0;

// Array to store list of background PIDs
pid_t bgPids[200];
int numBgPids = 0;

// Status of last freground command
int lastStatus = 0;

/**
 *  @struct command
 *  @brief Stores parsed user command and arguments.
 *  @var command::args
 *  Member 'args' stores a list of strings representing parsed arguments.
 *  @var command::argc
 *  Member 'argc' stores the number of arguments.
 *  @var command::inFile
 *  Member 'inFile' stores the name of the file to be used for standard input, or NULL if no file was specified.
 *  @var command::outFile
 *  Member 'outFile' stores the name of the file to be used for standard output, or NULL if no file was specified.
 *  @var command::bg
 *  Member 'bg' is a boolean value representing whether the process should be run in the background.
 */struct command {
	char *args[MAX_ARGS + 1];
	int argc;
	char *inFile;
	char *outFile;
	int bg;
};

/*
 * Parses a given line of command
 *
 * line: The command line to be parsed
 * cmd: The command struct to be filled
 *
 * returns: 1 if input was a comment or empty, 0 otherwise
 */
int parseCmd(char* line, struct command* cmd) {
	// Print shell prompt and get input
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
	
	// Loop over and tokenize string into argc
	// Skip "<", ">", and "&". Use to set special arguments
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

/**
 * @brief Runs the cd built-in command
 *
 * This function is used to change the current working directory of the shell.
 *
 * @param cmd The command struct containing the arguments to the cd command
 */
void runCd(struct command cmd) {
	// Cd to given directory, if no given, cd to home
	if (cmd.argc > 1 && chdir(cmd.args[1])) {
		fprintf(stderr, "Could not find %s\n", cmd.args[1]);
		fflush(stdout);
	} else if (cmd.argc == 1)
		chdir(getenv("HOME"));
}

/**
 * @brief Runs the status built-in command
 *
 * This function is used to print the exit status of the last process, or the termination signal of the last process if it was terminated by a signal.
 *
 * @param lastStatus The status of the last process
 */
void runStatus(int lastStatus) {
	// Check if process exited normally or was terminated by a signal
	if (WIFEXITED(lastStatus))
		// If exited normally, print exit value
		printf("exit value %d\n", WEXITSTATUS(lastStatus));
	else if (WIFSIGNALED(lastStatus))
		// If terminated by signal, print signal number
		printf("terminated by signal %d\n", WTERMSIG(lastStatus));
	fflush(stdout);
}

/**
 * @brief Checks if any background processes have completed
 *
 * This function will check if any background processes have completed and, if so, will print the exit status of the process and remove it from the list of background processes.
 *
 * @param bgPids An array of PIDs of background processes
 * @param numBgPids The number of background processes
 * @param status The status of the last process
 * @param terminate A boolean value indicating whether to terminate the shell
 */
void checkBg(int* bgPids, int* numBgPids, int* status, int terminate) {
	// Loop over background PIDs
	for (int i = 0; i < *numBgPids; i++) {
		// Check if background process has completed
		pid_t pid = waitpid(bgPids[i], status, WNOHANG);
		
		// Terminate process if specified
		if (terminate)
			kill(bgPids[i], SIGKILL);
		
		// Print exit status and remove from background PIDs list
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
	
	// Exit smallsh when specified
	if (terminate)
		exit(0);
}

/**
 * @brief Redirects Standard Input/Output to/from a file
 *
 * This function is used to redirect standard input/output to/from a file instead of the terminal.
 *
 * @param file The name of the file to use for input/output
 * @param redirFd The file descriptor to be redirected (0 for standard input, 1 for standard output)
 * @param flags The flags to use when opening the file
 * @param action The type of action being performed ("input" or "output")
 */
void redirectIO(char* file, int redirFd, int flags, char* action) {
	// If file specified, open with given flags
	if (file != NULL) {
		int fd = open(file, flags, 0644);
		
		// Error on open() failure
		if (fd == -1) {
			fprintf(stderr, "cannot open %s for %s\n", file, action);
			fflush(stdout);
			exit(1);
		}
		
		// Redirect file descriptor
		dup2(fd, redirFd);
		close(fd);
	}
}

/**
 * @brief Creates a new process to execute a given command
 *
 * This function is used to create a new process and execute the given command. It will redirect the standard input/output if necessary, and handle background processes.
 *
 * @param cmd The command to be executed
 * @param sa The signal action to be used
 */
void newProcess(struct command cmd, struct sigaction sa) {
	// Fork for new process
	pid_t pid = fork();
	
	if (pid == -1) {						// Fork Failule
		perror("fork() failed!");
		exit(1);
	} else if (pid == 0) {					// Child Process
		// Default sig handler if not background process
		if (!cmd.bg) {
			sa.sa_handler = SIG_DFL;
			sa.sa_flags = 0;
			sigaction(SIGINT, &sa, NULL);
		}
		
		// Redirect standard input/output
		redirectIO(cmd.inFile, 0, O_RDONLY, "input");
		redirectIO(cmd.outFile, 1, O_WRONLY | O_CREAT | O_TRUNC, "output");
		
		// Execute command and print errors
		execvp(cmd.args[0], cmd.args);
		fprintf(stderr, "%s: no such file or directory\n", cmd.args[0]);
		fflush(stdout);
		exit(1);
	} else {								// Parent Process
		// If backgroud and valid, add process to background PIDs
		if (cmd.bg && !fgMode) {
			// Print background process ID
			bgPids[numBgPids++] = pid;
			printf("background pid is %d\n", pid);
			fflush(stdout);
		} else {
			// Otherwise, wait for child process to complete
			waitpid(pid, &lastStatus, 0);
		}
	}
	
	// Reset file descriptors
	if (cmd.inFile || cmd.outFile)
		dup2(0, 0), dup2(1, 1);
}

/**
 * @brief Sets the signal handler for a given signal
 *
 * This function is used to set the signal handler for a given signal.
 *
 * @param signum The signal to be handled
 * @param handler The signal handler function to be used
 * @param flags The flags to be used
 * @param sa The sigaction struct to be filled
 */
void set_signal_handler(int signum, void (*handler)(int), int flags, struct sigaction *sa) {
	sa->sa_handler = handler;
	sigfillset(&sa->sa_mask);
	sa->sa_flags = flags;
	sigaction(signum, sa, NULL);
}

/**
 * @brief Handles the SIGTSTP signal
 *
 * This function is used to handle the SIGTSTP signal by switching the shell between foreground-only mode and normal mode.
 *
 * @param signo The signal to be handled
 */
void handle_SIGTSTP(int signo) {
	// Set mesage based on fgMode, then toggle fgMode
	char* message = fgMode ? "\nExiting foreground-only mode\n: " : "\nEntering foreground-only mode (& is now ignored)\n: ";
	fgMode = !fgMode;
	write(STDOUT_FILENO, message, strlen(message));
	fflush(stdout);
}

/**
 * @brief Main function for the shell
 *
 * This function is the main function for the shell. It will parse user commands, handle built-in commands, and create new processes to run other commands.
 *
 * @param argc The number of command line arguments
 * @param argv The arguments given on the command line
 *
 * @return 0 on success, 1 on error
 */
int main(int argc, const char * argv[]) {
	// Create sig actions for SIGINT and SIGTSTP
	struct sigaction sa = {0}, SIGTSTP_action = {0};
	set_signal_handler(SIGINT, SIG_IGN, 0, &sa);
	set_signal_handler(SIGTSTP, handle_SIGTSTP, SA_RESTART, &sa);
	
	char line[MAX_CMD_LENGTH];
	
	// Command Loop (exits on "exit" command)
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
