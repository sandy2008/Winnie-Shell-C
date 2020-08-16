#include <unistd.h>
#include <sys/wait.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

const unsigned MAX_LINE_LENGTH = 89;
const unsigned BUF_SIZE = 64;
const unsigned REDIR_SIZE = 2;
const unsigned PIPE_SIZE = 3;
const unsigned MAX_HISTORY = 30;
const unsigned MAX_COMMAND_NAME = 30;
const unsigned ANTI_CHINA_WORDLIST_SIZE = 64;
const char *ANTI_CHINA_WORDLIST[ANTI_CHINA_WORDLIST_SIZE] = {"xjp","winnie","democracy","freedom","justice"};

void parse_cmd(char input[], char *argv[], int *wait)
{
	for (unsigned idx = 0; idx < BUF_SIZE; idx++)
	{
		argv[idx] = NULL;
	}

	if (input[strlen(input) - 1] == '&')
	{
		*wait = 1;
		input[strlen(input) - 1] = '\0';
	}
	else
	{
		*wait = 0;
	}

	const char *delim = " ";
	unsigned idx = 0;

	char *token = strtok(input, delim);
	while (token != NULL)
	{
		argv[idx++] = token;
		token = strtok(NULL, delim);
	}

	argv[idx] = NULL;
}

void parse_redir(char *argv[], char *redir_argv[])
{
	unsigned idx = 0;
	redir_argv[0] = NULL;
	redir_argv[1] = NULL;

	while (argv[idx] != NULL)
	{

		if (strcmp(argv[idx], "<") == 0 || strcmp(argv[idx], ">") == 0)
		{

			if (argv[idx + 1] != NULL)
			{

				redir_argv[0] = strdup(argv[idx]);
				redir_argv[1] = strdup(argv[idx + 1]);
				argv[idx] = NULL;
				argv[idx + 1] = NULL;
			}
		}

		idx++;
	}
}

int parse_pipe(char *argv[], char *child01_argv[], char *child02_argv[])
{
	unsigned idx = 0, split_idx = 0;
	int contains_pipe = 0;
	int cnt = 0;

	while (argv[idx] != NULL)
	{

		if (strcmp(argv[idx], "|") == 0)
		{
			split_idx = idx;
			contains_pipe = 1;
		}
		idx++;
	}

	if (!contains_pipe)
	{
		return 0;
	}

	for (idx = 0; idx < split_idx; idx++)
	{
		child01_argv[idx] = strdup(argv[idx]);
	}
	child01_argv[idx++] = NULL;

	while (argv[idx] != NULL)
	{
		child02_argv[idx - split_idx - 1] = strdup(argv[idx]);
		idx++;
	}
	child02_argv[idx - split_idx - 1] = NULL;

	return 1;
}

void child(char *argv[], char *redir_argv[])
{
	int fd_out, fd_in;
	if (redir_argv[0] != NULL)
	{

		if (strcmp(redir_argv[0], ">") == 0)
		{

			fd_out = creat(redir_argv[1], S_IRWXU);
			if (fd_out == -1)
			{
				perror("Redirect output failed");
				exit(EXIT_FAILURE);
			}

			dup2(fd_out, STDOUT_FILENO);

			if (close(fd_out) == -1)
			{
				perror("Closing output failed");
				exit(EXIT_FAILURE);
			}
		}

		else if (strcmp(redir_argv[0], "<") == 0)
		{
			fd_in = open(redir_argv[1], O_RDONLY);
			if (fd_in == -1)
			{
				perror("Redirect input failed");
				exit(EXIT_FAILURE);
			}

			dup2(fd_in, STDIN_FILENO);

			if (close(fd_in) == -1)
			{
				perror("Closing input failed");
				exit(EXIT_FAILURE);
			}
		}
	}

	if (execvp(argv[0], argv) == -1)
	{
		perror("Fail to execute command");
		exit(EXIT_FAILURE);
	}
}

void parent(pid_t child_pid, int wait)
{
	int status;
	printf("Parent <%d> spawned a child <%d>.\n", getpid(), child_pid);
	switch (wait)
	{

	case 0:
	{
		waitpid(child_pid, &status, 0);
		break;
	}

	default:
	{
		waitpid(child_pid, &status, WUNTRACED);

		if (WIFEXITED(status))
		{
			printf("Child <%d> exited with status = %d.\n", child_pid, status);
		}
		break;
	}
	}
}

void add_history_feature(char *history[], int history_count, char *input_line)
{

	if (history_count < MAX_HISTORY)
	{
		strcpy(history[history_count++], input_line);
	}
	else
	{
		free(history[0]);
		for (int i = 1; i < MAX_HISTORY; i++)
		{
			strcpy(history[i - 1], history[i]);
		}

		strcpy(history[MAX_HISTORY - 1], input_line);
	}
}

void exec_with_pipe(char *child01_argv[], char *child02_argv[])
{
	int pipefd[2];

	if (pipe(pipefd) == -1)
	{
		perror("pipe() failed");
		exit(EXIT_FAILURE);
	}

	if (fork() == 0)
	{

		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[0]);
		close(pipefd[1]);

		execvp(child01_argv[0], child01_argv);
		perror("Fail to execute first command");
		exit(EXIT_FAILURE);
	}

	if (fork() == 0)
	{

		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[1]);
		close(pipefd[0]);

		execvp(child02_argv[0], child02_argv);
		perror("Fail to execute second command");
		exit(EXIT_FAILURE);
	}

	close(pipefd[0]);
	close(pipefd[1]);

	wait(0);

	wait(0);
}

int main()
{
	int running = 1;
	pid_t pid;
	int status = 0, history_count = 0, wait;
	char input_line[MAX_LINE_LENGTH];
	char *argv[BUF_SIZE], *redir_argv[REDIR_SIZE];
	char *child01_argv[PIPE_SIZE], *child02_argv[PIPE_SIZE];
	char *history[MAX_HISTORY];

	for (int i = 0; i < MAX_HISTORY; i++)
	{
		history[i] = (char *)malloc(MAX_COMMAND_NAME);
	}

	while (running)
	{
		printf("winniesh>");
		fflush(stdout);

		while (fgets(input_line, MAX_LINE_LENGTH, stdin) == NULL)
		{
			perror("Cannot read user input!");
			fflush(stdin);
		}

		input_line[strcspn(input_line, "\n")] = '\0';

		add_history_feature(history, history_count, input_line);
		parse_cmd(input_line, argv, &wait);
		parse_redir(argv, redir_argv);

		if (strcmp(input_line, "exit") == 0)
		{
			running = 0;
			continue;
		}

		if (strcmp(input_line, "!!") == 0)
		{
			if (history_count == 0)
			{
				fprintf(stderr, "No commands in history\n");
				continue;
			}
			strcpy(input_line, history[history_count - 1]);
			printf("winniesh>%s\n", input_line);
		}

		if (strcmp(input_line, "history") == 0)
		{
			for (int k = 0; k < history_count; ++k)
			{
				printf("%s\n", history[k]);
			}
			continue;
		}

		if (strcmp(input_line, "hk") == 0)
		{
			printf("光復香港 時代革命\n");
			// 五大诉求 缺一不可
			continue;
		}

		for (int count = 0; count < sizeof(ANTI_CHINA_WORDLIST) / sizeof(char *); count++){
			if (strcmp(input_line, ANTI_CHINA_WORDLIST[count]) == 0)
			{
				printf("本代码不欢迎反华分子使用\n");
				exit(EXIT_FAILURE);
			}
		}

		if (strcmp(argv[0], "cd") == 0)
		{

			if (argv[1] == NULL)
			{
				chdir("/");
				continue;
			}
			else
			{
				chdir(argv[1]);
				continue;
			}
		}

		if (parse_pipe(argv, child01_argv, child02_argv))
		{
			exec_with_pipe(child01_argv, child02_argv);
			continue;
		}

		pid_t pid = fork();

		switch (pid)
		{
		case -1:
			perror("fork() failed!");
			exit(EXIT_FAILURE);

		case 0:
			child(argv, redir_argv);
			exit(EXIT_SUCCESS);

		default:
			parent(pid, wait);
		}
	}
	return 0;
}
