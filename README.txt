Assignment 3: smallsh (Portfolio Assignment)
- Provide a prompt for running commands
- Handle blank lines and comments, which are lines beginning with the # character
- Provide expansion for the variable $$
- Execute 3 commands exit, cd, and status via code built into the shell
- Execute other commands by creating new processes using a function from the exec family of functions
- Support input and output redirection
- Support running commands in foreground and background processes
- Implement custom handlers for 2 signals, SIGINT and SIGTSTP

Compile using the command below:
gcc --std=gnu99 -o smallsh main.c

Run using the command below:
./smallsh
