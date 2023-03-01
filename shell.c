#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wait.h>
#include <fcntl.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>

#define ESCAPES "'\""
#define PARAM_DELIMITERS " "
#define CMD_DELIMITERS "\n"

#define STOP 0
#define END 1
#define CONT 2
#define MEMORY_ERR 3
#define PIPE_ERR 4
#define PROCESS_ERR 5
#define SYNTAX_ERR 6
#define COMMAND_SEARCH_ERR 7
#define FILE_SEARCH_ERR 8
#define FILE_CREATION_ERR 9

#define MEMORY_ERR_MSG "Not enough memory"
#define PIPE_ERR_MSG "Failed to create pipe"
#define PROCESS_ERR_MSG "Failed to create process"
#define SYNTAX_ERR_MSG "Syntax error"
#define COMMAND_SEARCH_ERR_MSG "Command %s not found"
#define FILE_SEARCH_ERR_MSG "File %s not found"
#define FILE_CREATION_ERR_MSG "File %s not found"
#define DIRECTORY_SEARCH_ERR_MSG "Directory %s not found"
#define SUCCESS_MSG "Shell is closing, bye"

#define INPUT_REDIRECTION "<"
#define OUTPUT_REDIRECTION ">"
#define OUTPUT_APPEND_REDIRECTION ">>"
#define ERROR_REDIRECTION "2>"

#define INITIAL_LENGTH 16

static int status;

int contains(char *str, size_t len, char c)
{	
	if (!str) return 0;
	while (len--)
		if (c == str[len]) return 1;
	return 0;	
}

int is_escape(char c) {return contains(ESCAPES, sizeof(ESCAPES)-1, c);}
int is_param_delimiter(char c) {return contains(PARAM_DELIMITERS, sizeof(PARAM_DELIMITERS)-1, c);}
int is_cmd_delimiter(char c) {return contains(CMD_DELIMITERS, sizeof(CMD_DELIMITERS)-1, c);}
int is_redirection(char *str) {return str && (!strcmp(str, INPUT_REDIRECTION) || !strcmp(str, OUTPUT_REDIRECTION) || !strcmp(str, OUTPUT_APPEND_REDIRECTION) || !strcmp(str, ERROR_REDIRECTION));}

char **get_cmd()
{
	static char *str = NULL;
	static size_t length = INITIAL_LENGTH;
	
	int c;
	char **cmd;
	char escape = 0;
	size_t nchars = 0;
	int nparams = 0;
	int null_param = 0;
	int nfiles_in_redirection = 0;
	
	char *redirections[] = {NULL, NULL, NULL};
	
	if (!str) str = (char *) malloc(INITIAL_LENGTH*sizeof(*str));
	
	if (!str)
	{
		status = MEMORY_ERR;
		return NULL;
	}
	
	for (;;)
	{
		c = getchar();
		
		if (c == EOF) goto stop; //Согласно книге язык программирования Си Кернигана и Ритчи, эта ситуация удобно решается таким способом.
		
		if ((c == '<' || c == '>' || c == '|' || is_cmd_delimiter(c)) && 
		(escape != 0 ||
		(!null_param && ((!is_cmd_delimiter(c) && (nchars == 0 || (nchars == 1 && c == '>' && str[nchars-1] == '2'))) || (redirections[0] && (str[nchars-2] == '<' || str[nchars-2] == '>'))))))
			goto stop;
		
		if (!null_param && nchars == 0 && is_cmd_delimiter(c))
		{
			status = END;
			free(str);
			str = NULL;
			return NULL;
		}
		
		if(escape == 0 && is_param_delimiter(c))
		{
			if ((nchars > 0 && str[nchars-1] != '\0') || null_param)
			{
				null_param = 0;
				c = '\0';
			} else continue;
		}
		
		if (is_escape(c))
		{
			if (escape == 0)
			{
				null_param = 1;
				escape = c;
				continue;
			} else if (c == escape)
			{
				escape = 0;
				continue;
			}
		}
		
		if (c == '<' || c == '>' || c == '|' || is_cmd_delimiter(c))
		{
			if ((nchars > 0 && str[nchars-1] != '\0' && (c != '>' || (str[nchars-1] != '>' && (str[nchars-1] != '2' || (nchars > 1 && str[nchars-2] != '\0'))))) || null_param)
			{
				ungetc(c, stdin);
				
				if (nchars > 0 && c == '>' && str[nchars-1] == '2' && ((nchars > 1 && str[nchars-2] != '\0') || null_param))
				{
					str[nchars-1] = '\0';
					nparams++;
					c = '2';	
				} else c = '\0';
				
				null_param = 0;
			}
			else if (c == '<' || c == '>')
			{
				if (c == '>')
				{
					c = getchar();
					if (c != EOF) ungetc(c, stdin);
					if (c != '>' || str[nchars-1] == '>' || str[nchars-1] == '2') ungetc('\0', stdin);
					c = '>';
				}
				else ungetc('\0', stdin);
			}
			else
			{
				if (c == '|') status = CONT;
				else status = END;
				break;
			}
		}
		
		
		if (c == '\0' && nchars > 0 && (str[nchars-1] == '<' || str[nchars-1] == '>'))
		{
			int i;
			char *redirection;
			
			if (str[nchars-1] == '<') redirection = INPUT_REDIRECTION;
			else
			{
				if (str[nchars-2] == '2') redirection = ERROR_REDIRECTION;
				else redirection = OUTPUT_REDIRECTION;
			}
			
			for (i = 0; redirections[i] != NULL && i < 3; i++)
				if (redirection == redirections[i]) goto stop;
			redirections[i] = redirection;
			nfiles_in_redirection = -1;
		}
		
		if (c == '\0')
		{
			nparams++;
			if (redirections[0]) nfiles_in_redirection++; 
						
			if (nfiles_in_redirection > 1) goto stop;
		}
		
		if (nchars == length-1)
		{
			char *tmp;
			length = length + (length >> 1);
			tmp = (char *) realloc(str, length*sizeof(*str));
			
			if (!tmp)
			{
				free(str);
				str = NULL;
				status = MEMORY_ERR;
				return NULL;
			}
			
			str = tmp;
		}
		
		str[nchars++] = c;
	}
	str[nchars] = '\0';
	
	cmd = (char **) malloc((nparams+2)*sizeof(*cmd));
	
	if (!cmd)
	{
		free(str);
		str = NULL;
		status = MEMORY_ERR;
		return NULL;
	}
	
	cmd[0] = str;
	
	{
		int with_redirections = 0;
		int i, j;
	
		for (i = 1, j = 0; i < nparams+1; i++)
		{	
			while(str[j++] != '\0');
			
			if (j == nchars) break;
				
			if (!with_redirections && is_redirection(str+j))
			{
				cmd[i++] = NULL;
				with_redirections = 1;
			}
			cmd[i] = str+j;
		}
		cmd[i] = NULL;

		if (!with_redirections) cmd[i+1] = NULL;
	}
	
	return cmd;
stop:
	free(str);
	str = NULL;
	
	while (c != EOF)
	{
		if (is_cmd_delimiter(c))
		{
			status = SYNTAX_ERR;
			return NULL;
		}
		
		c = getchar();
	}
	
	status = STOP;
	return NULL;
}

void print_error(char *str)
{
	switch (status)
	{
		case MEMORY_ERR:
			fprintf(stderr, MEMORY_ERR_MSG "\n");
			return;
		case PIPE_ERR:
			fprintf(stderr, PIPE_ERR_MSG "\n");
			return;
		case PROCESS_ERR:
			fprintf(stderr, PROCESS_ERR_MSG "\n");
			return;
		case SYNTAX_ERR:
			fprintf(stderr, SYNTAX_ERR_MSG "\n");
			return;
		case COMMAND_SEARCH_ERR:
			fprintf(stderr, COMMAND_SEARCH_ERR_MSG "\n", str ? str : "");
			return;
		case FILE_SEARCH_ERR:
			fprintf(stderr, FILE_SEARCH_ERR_MSG "\n", str ? str : "");
			return;
		case FILE_CREATION_ERR:
			fprintf(stderr, FILE_CREATION_ERR_MSG "\n", str ? str : "");
			return;
	}
}

void pwd(void)
{
	char *wd = getcwd(NULL, PATH_MAX);
	if (wd) printf("%s\n", wd);
	else fprintf(stderr, MEMORY_ERR_MSG "\n");
	free(wd);
}

void cd(char *path)
{
	int c;
	
	if (!path) c = chdir(getenv("HOME"));
	else if (strlen(path) == 0) return;
	else c = chdir(path);
	
	if (c < 0)
	{
		if (errno == ENOMEM)
			fprintf(stderr, MEMORY_ERR_MSG "\n");
		else
			fprintf(stderr, DIRECTORY_SEARCH_ERR_MSG "\n", path == NULL ? "" : path);
	}
}

int main(void)
{
	char **cmd = NULL;
	int pipes[][2] = {{-1, -1}, {-1, -1}};
	pid_t pid;
	int k;
	
	k = 2;
	for (;;)
	{
		free(cmd);
		
		cmd = get_cmd();
		
		if (status == STOP || (cmd && !strcmp(cmd[0], "exit")))
		{
			if (status != STOP) printf(SUCCESS_MSG "\n");
			close(pipes[(k+1)%2][0]);
			status = STOP;
			break;
		}
		
		if (cmd && !strcmp(cmd[0], "cd"))
		{
			if (k == 2 && status == END) cd(cmd[1]);
			else if (status != END)
			{
				close(pipes[(k+1)%2][0]);
				if (pipe(pipes[(k+1)%2]) < 0)
				{
					status = PIPE_ERR;
					print_error(NULL);
					break;
				}
				close(pipes[(k+1)%2][1]);
				k = 0;
			}
			
			continue;
		}
		
		if (!cmd || pipe(pipes[k%2]) < 0)	
		{
			close(pipes[(k+1)%2][0]);
			pipes[(k+1)%2][0] = -1;
			
			if (status != SYNTAX_ERR && status != END)
			{
				if (status != MEMORY_ERR) status = PIPE_ERR;
				print_error(NULL);
				break;
			}
			
			if (status == SYNTAX_ERR || (status == END && k != 2))
			{
				status = SYNTAX_ERR;
				print_error(NULL);
			}
			
			k = 2;
			continue;
		}
		
		fcntl(pipes[k%2][1], F_SETFL, O_NONBLOCK);
		
		if (!(pid = fork()))
		{
			int i = 0;
			while (cmd[i++] != NULL);
			
			close(pipes[k%2][0]);
			if (k != 2) dup2(pipes[(k+1)%2][0], 0);
			if (status != END) dup2(pipes[k%2][1], 1);
			close(pipes[(k+1)%2][0]);
			
			if (cmd[i])
			{
				while (cmd[i])
				{
					int fd;
					
					if (!strcmp(cmd[i], INPUT_REDIRECTION))
					{
						fd = open(cmd[i+1], O_RDONLY);
						if (fd > 0) dup2(fd, 0);
						else status = FILE_SEARCH_ERR;
					}
					else if (!strcmp(cmd[i], OUTPUT_APPEND_REDIRECTION))
					{
						fd = open(cmd[i+1], O_WRONLY);
						if (fd < 0) fd = open(cmd[i+1], O_WRONLY | O_CREAT | O_EXCL, 0664);
						if (fd > 0)
						{
							lseek(fd, 0, SEEK_END);
							dup2(fd, 1);
						} else status = FILE_CREATION_ERR;
					}
					else
					{
						
						fd = open(cmd[i+1], O_WRONLY | O_TRUNC);
						if (fd < 0) fd = open(cmd[i+1], O_WRONLY | O_CREAT | O_EXCL, 0664);
						if (fd > 0)
						{
							if (!strcmp(cmd[i], ERROR_REDIRECTION)) dup2(fd, 2);
							else dup2(fd, 1);
						}
						else status = FILE_CREATION_ERR;
					}
					
					close(fd);			
					
					if (fd < 0)
					{
						dup2(pipes[k%2][1], 2);
						close(pipes[k%2][1]);
						print_error(cmd[i+1]);
						free(cmd[0]);
						free(cmd);
						return status;
					}
					
					i += 2;
				}
				
			}
			
			close(pipes[k%2][1]);
			
			if (!strcmp(cmd[0], "pwd"))
			{
				free(cmd[0]);
				free(cmd);
				pwd();
				return 0;
			}
			execvp(cmd[0], cmd);
			
			free(cmd[0]);
			free(cmd);
			
			return COMMAND_SEARCH_ERR;
		} else 
		{
			close(pipes[(k+1)%2][0]);
			close(pipes[k%2][1]);
			
			if(pid > 0)
			{
				int info, exit_code;
				wait(&info);
				
				exit_code = WEXITSTATUS(info);				
				
				if (exit_code == COMMAND_SEARCH_ERR || exit_code == FILE_SEARCH_ERR || exit_code == FILE_CREATION_ERR)
				{
					int c;
					if (status != END)
						while ((c = getchar()) != EOF && !is_cmd_delimiter(c));
					
					if (status == END || c != EOF)
					{
						if (exit_code == COMMAND_SEARCH_ERR)
						{
							status = COMMAND_SEARCH_ERR;
							print_error(cmd[0]);
						}
						else
							while (read(pipes[k%2][0], &c, sizeof(char)))
								fprintf(stderr, "%c", c);
					}
					
					close(pipes[k%2][0]);
					pipes[k%2][0] = -1;
					status = exit_code;
					k = 2;
					continue;
				}
			
				if (status == END)
				{
					close(pipes[k%2][0]);
					pipes[k%2][0] = -1;
					k = 2;
					continue;
				}
				
				if (k == 2) k = 1;
				else k = (k+1)%2;
			} else 
			{
				close(pipes[k%2][0]);
				status = PROCESS_ERR;
				print_error(NULL);
				break;
			}
		}
	}
	
	if (cmd)
	{
		free(cmd[0]);
		free(cmd);
	}
	
	return status;
}