#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

struct parsed_info {
    int background;  //if command is to be run in bg
    char *parsed_array[512];
    char *infile;
    char *outfile;
    int array_length;
};

static int word_split(char **string_pointers, char *line, char *delimiter);
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub);
static int word_expansion(char **string_pointers, int len_words);
static int parse_words(char **string_pointers, struct parsed_info *parsed);
void exit_command(struct parsed_info *parsed);
static int cd_command(struct parsed_info *parsed);
void exec_foreground(struct parsed_info *parsed);
void exec_background(struct parsed_info *parsed);

//handler for SIGINT
void handle_SIGINT(int signo) {

}


int fg_exit_status = 0;
char *recent_bg_pid = "";

int main(void)
{
start:
  signal(SIGTSTP, SIG_IGN);

  errno = 0;

  char *line = NULL;
  size_t n = 0;
 
  char *delimiter = getenv("IFS") ? getenv("IFS") : " \n\t";

  struct parsed_info parsed;
  struct parsed_info *parsed_ptr = &parsed;
 
  int max_word = 512;
  char *string_pointers[max_word];

  //create struct for signals
  struct sigaction SIGINT_action = {0};
 


  for (;;) {
   
    //check for un-waited-for bg processes
   
    int status;
    int exit_status;
    pid_t return_value;
    while ((return_value = waitpid(0, &status, WNOHANG | WUNTRACED)) > 0){
        if (WIFEXITED(status)){
            exit_status = WEXITSTATUS(status);
            fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) return_value, exit_status);
        }
        else if (WIFSIGNALED(status)) {
            exit_status = WTERMSIG(status);
            fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) return_value, exit_status);
        }
        else if (WIFSTOPPED(status)) {
            kill(return_value, SIGCONT);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) return_value);
        }
       
    }


    if (getenv("PS1")) {
    fprintf(stderr, "%s", getenv("PS1"));
    } else fprintf(stderr, "");

    //register handle_SIGINT as signal handler
    SIGINT_action.sa_handler = handle_SIGINT;
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_action, NULL);

 
    ssize_t line_length = getline(&line, &n, stdin);

    if (line_length == -1){
      if (feof(stdin) != 0) {
        goto exit;
      } else {
          clearerr(stdin);
          goto start;
      }
    }

    //update sigint_action to SIG_IGN to be ignored
    SIGINT_action.sa_handler = SIG_IGN;
    sigaction(SIGINT, &SIGINT_action, NULL);

    int len_words;
    len_words = word_split(string_pointers, line, delimiter);
    if (len_words < 1) {
       goto clear;
    }
    
    word_expansion(string_pointers, len_words);
    parse_words(string_pointers, parsed_ptr);
    

    // if no command word, return to step 1
    if (parsed.array_length < 1){
        goto clear;
    }

    //check for built in command exit
    if (strcmp(parsed.parsed_array[0], "exit") == 0 ) {
        exit_command(parsed_ptr);
    }

    //check for built in command cd
    if (strcmp(parsed.parsed_array[0], "cd") == 0) {
        cd_command(parsed_ptr);
        goto clear;
    }

    //check if non-built in command should be run in fg or bg
    if (parsed.background == 1) {
        exec_foreground(parsed_ptr);
    }

    if (parsed.background == 0) {
        exec_background(parsed_ptr);
    }
   

    //clear array
clear:;
    int i;
    for (i=0; i<512; i++){
        string_pointers[i] = NULL;
    }

    for (i=0; i<512; i++){
      parsed.parsed_array[i] = NULL;
    }
    goto start;
 
  }
exit:;
     //clear arrays
    int i;
    for (i=0; i<512; i++){
        string_pointers[i] = NULL;
    }

    for (i=0; i<512; i++){
      parsed.parsed_array[i] = NULL;
    }

     fprintf(stderr, "\nexit\n");
     exit(fg_exit_status);
}

static int word_split(char **string_pointers, char *line, char *delimiter){
   
    int i = 0;
    char *token = strtok(line, delimiter);

    while (token != NULL) {
      string_pointers[i] = strdup(token);
      token = strtok(NULL, delimiter);
      i++;
    }
    //null value at end
    string_pointers[i] = token;
    return i;
}

// str_gsub function from Ryan Gambord's instructional video
char *str_gsub(char *restrict *restrict haystack, char const *restrict needle, char const *restrict sub){
    char *str = *haystack;
    size_t haystack_len = strlen(str);
    size_t const needle_len = strlen(needle),
                 sub_len = strlen(sub);

    for (; (str = strstr(str, needle));) {
        ptrdiff_t off = str - *haystack;
        if (sub_len > needle_len) {
            str = realloc(*haystack, sizeof **haystack * (haystack_len + sub_len - needle_len + 1));
            if (!str) {
              goto exit;
            }
            *haystack = str;
            str = *haystack + off;
        }

        memmove(str + sub_len, str + needle_len, haystack_len + 1 - off - needle_len);
        memcpy(str, sub, sub_len);
        haystack_len = haystack_len + sub_len - needle_len;
        str += sub_len;
    }
    str = *haystack;
    if (sub_len < needle_len) {
        str = realloc(*haystack, sizeof **haystack * (haystack_len + 1));
        if (!str) {
            goto exit;
        }
        *haystack = str;
    }
exit:
    return str;
}

static int word_expansion(char **string_pointers, int len_words){
   
    int i;
    //convert pid integer to string
    pid_t pid = getpid();
    char pid_string[20];
    sprintf(pid_string, "%jd", (intmax_t) pid);
    char *home = getenv("HOME") ? getenv("HOME") : "";
    //set $? from fg_exit_status
   
    char *exit_status_string = "";
    exit_status_string = malloc(10);
    sprintf(exit_status_string, "%d", fg_exit_status);
    char *command_exit_status = exit_status_string;
    //set $! from recent_bg_pid
    char *bg_pid = recent_bg_pid;



    for (i = 0; i < len_words; i++){
      //if ~/ is at start of word, replace with HOME env
      if (strncmp(string_pointers[i], "~/", 2) == 0) {
          str_gsub(&string_pointers[i], "~", home);
      }
      //any occurrence of $$ shall be replaced with the process ID (GETPID())
      str_gsub(&string_pointers[i], "$$", pid_string);
      //any occurrence of $? shall be replaced with exit status of last foreground command or 0
      str_gsub(&string_pointers[i], "$?", command_exit_status);
      //any occurrence of $! shall be replaced with pid of most recent bg process
      str_gsub(&string_pointers[i], "$!", bg_pid);
  }
    //clear exit status string
    free(exit_status_string);
    return 0;
}

static int parse_words(char **string_pointers, struct parsed_info *parsed){
     

    //transfer to struct array
    int i = 0;
    while (string_pointers[i] != NULL) {
        parsed->parsed_array[i] = strdup(string_pointers[i]);
        i++;
    }
    parsed->parsed_array[i] = NULL;
   
    parsed->array_length = i;
    i = 0;
    // find first occurrence of #
    int comment_idx = -1;
    while ((parsed->parsed_array[i] != NULL) && (comment_idx < 0)) {
        if (strcmp(parsed->parsed_array[i], "#") == 0) {
            comment_idx = i;
        } else i++;
    }

    //if # in words, nullify
    i = comment_idx;
    if (comment_idx >= 0) {
        while (parsed->parsed_array[i] != NULL) {
            parsed->parsed_array[i] = NULL;
            i++;
        }
        parsed->array_length = comment_idx;
    }
   
    //if & is last word, command is to be run in bg
    int len = parsed->array_length;
    if (strcmp(parsed->parsed_array[len-1], "&") == 0){
        parsed->background = 0; // command is run in bg, true
        parsed->parsed_array[len-1] = NULL;
        parsed->array_length = len-1;
    } else parsed->background = 1; //command is not run in bg, false

   
    //if the last word is preceded by < or >, filename and input/output
    //set infile and outfile to null
     parsed->infile = NULL;
     parsed->outfile = NULL;
    //only check if array is at least length 2
    //loop through twice to catch if both < and > are present at end
    int x = 2;
    while (x > 0) {
     len = parsed->array_length;
    if (len >= 2) {
        if (strcmp(parsed->parsed_array[len-2], "<") == 0) {
            parsed->infile = parsed->parsed_array[len-1];
            parsed->parsed_array[len-1] = NULL;
            parsed->parsed_array[len-2] = NULL;
            parsed->array_length -= 2;
        } else if (strcmp(parsed->parsed_array[len-2], ">") == 0) {
          parsed->outfile = parsed->parsed_array[len-1];
          parsed->parsed_array[len-1] = NULL;
          parsed->parsed_array[len-2] = NULL;
          parsed->array_length -= 2;
        }
    }
    x--;
    }

    return 0;

}

void exit_command(struct parsed_info *parsed){
  //if no arguments, use $?
  char exit_status_string[20];
  sprintf(exit_status_string, "%d", fg_exit_status);
  char *no_exit_arg = exit_status_string;
 
  int exit_value;

  if (parsed->array_length == 1) {
    exit_value = atoi(no_exit_arg);
    goto exit;
  }
 
  // if more than 1 arg, error
  if (parsed->array_length > 2) {
    goto error;
  }
 
  //if arg is not an int, error
  int is_int = atoi(parsed->parsed_array[1]);
  //if is int is 0 and the arg is not actually 0, arg is not an int
  if ((is_int == 0) && (strcmp(parsed->parsed_array[1], "0") != 0)) {
    goto error;
  } else {
      exit_value = is_int;
      goto exit;
  }


exit:
  fprintf(stderr, "\nexit\n");
  kill(0, SIGINT);
  exit(exit_value);

error:
  fprintf(stderr, "\nExit Error\n");

}

static int cd_command(struct parsed_info *parsed){
  char *no_cd_arg = getenv("HOME") ? getenv("HOME") : "";
  char *path;
  int cd_error;

  //if too many args, error
  if (parsed->array_length > 2) {
    goto error;
  }

  //if no arg, home implied
  if (parsed->array_length == 1) {
    path = no_cd_arg;
  } else {
    path = parsed->parsed_array[1];
  }

 
  cd_error = chdir(path);

  if (cd_error == -1){
    goto error;
  } else {
    return 0;
  }

error:
  fprintf(stderr, "\ncd Error\n");
  return 1;
}

void exec_foreground(struct parsed_info *parsed){
    int child_status;
   
    //fork a new process  
    pid_t spawn_pid = fork();
   

    if (spawn_pid == -1) {
     
        perror("fork error\n");
        exit(1);
    }
    else if (spawn_pid == 0) {
        //reset signal
        signal(SIGINT, SIG_DFL);

        //handle input redirect before exec
        if (parsed->infile != NULL) {
           int file_descriptor = open(parsed->infile, O_RDONLY);

           if (file_descriptor == -1){
              perror("Open() Failed");
              exit(1);
           }
           
           //dup2 with 0 points file_descriptor to stdin
           int result = dup2(file_descriptor, 0);

          if (result == -1) {
              perror("dup2");
              exit(2);
          }
         
          //close file descriptor
          if (close(file_descriptor) == -1) {
              perror("close error");
          }

        }

        //handle output redirect before exec
        if (parsed->outfile != NULL) {
           int file_descriptor = open(parsed->outfile, O_RDWR | O_CREAT, 0777);
       
       
        if (file_descriptor == -1){
              perror("Open() Failed");
              exit(1);
           }

        //dup2 with 1 points file_descriptor to stdout
        int result = dup2(file_descriptor, 1);

        if (result == -1) {
            perror("dup2");
            exit(1);
        }

        //close file descriptor
        if (close(file_descriptor) == -1) {
            perror("close error");
        }

      }

        execvp(parsed->parsed_array[0], parsed->parsed_array);
        perror("execvp error");
        exit(2);
    }

    spawn_pid = waitpid(spawn_pid, &child_status, 0);
   
    int exit_status;
   
    //check if exited normally
    if (WIFEXITED(child_status)){
        exit_status = WEXITSTATUS(child_status);
        fg_exit_status = exit_status;
    }

    else if (WIFSIGNALED(child_status)){
        exit_status = WTERMSIG(child_status);
        fg_exit_status = (128 + exit_status);

    }
    else if (WIFSTOPPED(child_status)) {
        kill(spawn_pid, SIGCONT);
        fprintf(stderr, "Child process %d stopped. Continuing.\n", spawn_pid);
        recent_bg_pid = malloc(10);
        sprintf(recent_bg_pid, "%d", spawn_pid);
       

    }
}

void exec_background(struct parsed_info *parsed){
    int child_status;
   
    //fork a new process
    pid_t spawn_pid = fork();

    if (spawn_pid == -1) {
        perror("fork error\n");
        exit(1);
    }
    else if (spawn_pid == 0) {
        //reset signal
        signal(SIGINT, SIG_DFL);

        //handle input redirect before exec
        if (parsed->infile != NULL) {
           int file_descriptor = open(parsed->infile, O_RDONLY);

           if (file_descriptor == -1){
              perror("Open() Failed");
              exit(1);
           }
           
           //dup2 with 0 points file_descriptor to stdin
           int result = dup2(file_descriptor, 0);

          if (result == -1) {
              perror("dup2");
              exit(2);
          }
         
          //close file descriptor
          if (close(file_descriptor) == -1) {
              perror("close error");
          }

        }

        //handle output redirect before exec
        if (parsed->outfile != NULL) {
           int file_descriptor = open(parsed->outfile, O_RDWR | O_CREAT, 0777);
       
       
        if (file_descriptor == -1){
              perror("Open() Failed");
              exit(1);
           }

        //dup2 with 1 points file_descriptor to stdout
        int result = dup2(file_descriptor, 1);

        if (result == -1) {
            perror("dup2");
            exit(1);
        }

        //close file descriptor
        if (close(file_descriptor) == -1) {
            perror("close error");
        }

      }
        execvp(parsed->parsed_array[0], parsed->parsed_array);
        perror("execvp error");
        exit(2);
    }

    recent_bg_pid = malloc(10);
    sprintf(recent_bg_pid, "%d", spawn_pid);
   
}