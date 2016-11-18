/* 
 * CS344 smallsh
 * by Allen Garvey
 */

/**
* Includes
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
//for character functions
#include <ctype.h>
//for changing responses to signals
#include <signal.h>
//for opening directories
#include <dirent.h>
//for checking directory errors
#include <errno.h>
//for chdir
#include <unistd.h>
//for pid
#include <sys/types.h>
//for waitpid
#include <sys/wait.h>
//for handling interrupts
#include <signal.h>
//for open
#include <fcntl.h>
#include <sys/stat.h>

/**
* Constants
*/
//maximum number of characters
//not including null char allowed in
//line of commands 
#define COMMAND_LINE_MAX_LENGTH 2048
//the maximum number of arguments allowed in a
//command line
#define MAX_ARGUMENT_COUNT 512

//character used at start of a line to define a comment
#define COMMENT_CHAR '#'

//used to represent uninitialized foreground pid
#define NULL_FOREGROUND_PID -1

//create custom bool class, since c99 is required for stdbool.h
typedef int BOOL;
//members of BOOL type
#define TRUE 1
#define FALSE 0


/*************************************
* Handling interrupts
**************************************/
//prototype for function so we can use it for interrupts
BOOL hasProcessStopped(pid_t childProcessId, pid_t waitpidResult, int status);
int printStatus(int returnStatusCode);

//global variable storing the pid of the current foreground process
//needs to be global variable, because there is no other way for the interrupt
//handler to get access to this pid
//NULL_FOREGROUND_PID represents there is no foregroundPid
//because no commands have been run yet, or last foreground command was built in command
//otherwise stores pid of last run foreground command
pid_t foregroundPid;
//global variable to store if foreground process stopped by interrupt
BOOL foregroundInterrupted;
//global variable to store signal number if foreground command is interrupted
int foregroundInterruptSignal;

//handles action for when user presses control-c when foreground process is running-
//it will kill that process and print a message saying so
//based on CS344 lecture 13 slides
void interruptHandler(int signalNum){
    //don't do anything if there is no foreground process running
    //check if foreground pid is even initialized or foreground has already been interrupted
    if(foregroundPid == NULL_FOREGROUND_PID || foregroundInterrupted == TRUE){
        return;
    }
    
    //must have foreground process, so send it the signal sent to the handler
    //based on: http://stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process
    //and http://www.csl.mtu.edu/cs4411.ck/www/NOTES/signal/kill.html
    kill(foregroundPid, signalNum);
    //set flags to show was interrupted
    foregroundInterrupted = TRUE;
    foregroundInterruptSignal = signalNum;
}

//called at the beginning of the program, it sets interruptHandler() to be called
//when user enters control-c
void initializeInterruptHandler(){
    //initialize foregroundPid to -1, because nothing should be happening now
    foregroundPid = NULL_FOREGROUND_PID;
    //initialized foreground interrupted flag to false
    foregroundInterrupted = FALSE;

    //struct to store signal action data
    struct sigaction act;
    //set function to be called on interrupt
    act.sa_handler = interruptHandler;
    act.sa_flags = 0;
    //initialize struct masks
    sigfillset(&(act.sa_mask));

    //set action on interrupt to use our struct
    sigaction(SIGINT, &act, NULL);
}



/*************************************
* Get user input functions
**************************************/

//gets user input, stores in commandLineBuffer 
//and chomps(deletes) trailing newline from pressing enter to input command
void getUserInput(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH]){
    //clear the buffer
    bzero(commandLineBuffer, COMMAND_LINE_MAX_LENGTH);
    //string is less than max, since we need to put newline at the end and null char at end
    fgets(commandLineBuffer, COMMAND_LINE_MAX_LENGTH - 1, stdin);
    //check to see if command ends in newline-if so remove it
    int length = strlen(commandLineBuffer);
    //start at last character (last character is technically \0), but not counted by strlen
    int indexOfLastChar = length - 1;
    //check if last char is newline, and if so, replace with null char
    if(commandLineBuffer[indexOfLastChar] == '\n'){
        commandLineBuffer[indexOfLastChar] = '\0';
    }
}

void writePrompt(){
	printf(": ");
}

/*************************************
* Parse user input functions
**************************************/
//returns 1 'true' if command matches 'cd' or 'cd <directory_name>', 
//0 'false' otherwise
int isCommandCD(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
    //sanity check - command must be at least length 2
    //to contain 'cd'
    if(bufferLength < 2){
        return 0;
    }
    //check for just plain 'cd'
    if(strcmp(commandLineBuffer, "cd") == 0){
        return 1;
    }
    //if length is less than 3 can't be 'cd <directory_name>'
    if(bufferLength < 3){
        return 0;
    }
    //check for 'cd <directory_name>' - must start with 'cd<space_character>'
    if(commandLineBuffer[0] == 'c' && commandLineBuffer[1] == 'd' && isspace(commandLineBuffer[2])){
        return 1;
    }
    //must be false if we get here
    return 0;

}

/*************************************
* Status functions
**************************************/
//prints the status code
//returns new status code which should always be 0
//since status command should never fail
int printStatus(int returnStatusCode){
    //check if last process was stopped by signal
    if(foregroundInterrupted == TRUE){
        printf("terminated by signal %d\n", foregroundInterruptSignal);
    }
    else{
        printf("exit value %d\n", returnStatusCode);
    }
    return 0;
}

/*************************************
* 'CD' functions
**************************************/
//executes 'cd' command in commandLineBuffer by
//changing current working directory to that given in the command, or the home directory
//if none given
//if there is an error - such as directory not readable, not existing, or a file and not a directory
//will print error message and not change working directory
//returns status code - 0 means success, 1 means there was an error
int executeCD(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
    //extract directory from command
    //initialize variable to hold directory
    char *directoryName;
    //need to know if we need to free directoryName, since
    //getting environment variable doesn't allocate space
    int didAllocateDirectoryName = 0;
    //if just 'cd', directoryName should be home directory
    if(strcmp(commandLineBuffer, "cd") == 0){
        //don't allocate memory for directoryName, since getenv returns
        //pointer to already existing string
        directoryName = getenv("HOME");
        //in the case that environment variable can't be found,
        //null is returned, so check for that, as that is an error
        if(directoryName == NULL){
            return 1;
        }
    }
    //else all data after 'cd ' is directory name
    else{
        //allocate memory for directoryName
        directoryName = malloc(sizeof(char) * COMMAND_LINE_MAX_LENGTH);
        //check malloc succeeded
        assert(directoryName != NULL);
        //save that we allocated memory
        didAllocateDirectoryName = 1;
        //fill with null bytes
        bzero(directoryName, COMMAND_LINE_MAX_LENGTH);
        //based on: http://stackoverflow.com/questions/6205195/how-can-i-copy-part-of-another-string-in-c-given-a-starting-and-ending-index
        //int startIndex = &commandLineBuffer + 2;
        strncpy(directoryName, (char *) &commandLineBuffer[3], bufferLength - 3);
    }
    //change working directory - if return value is -1 there were errors, 0 means it succeeded
    //based on: https://www.gnu.org/software/libc/manual/html_node/Working-Directory.html
    //and http://stackoverflow.com/questions/12510874/how-can-i-check-if-a-directory-exists
    //and http://pubs.opengroup.org/onlinepubs/009695399/functions/opendir.html
    int changeWorkingDirectoryReturnValue = chdir(directoryName);
    if(changeWorkingDirectoryReturnValue == 0){
        //directory exists, and is readable and user has permissions
        //free memory if we allocated it
        if(didAllocateDirectoryName){
            free(directoryName);
        }
        return 0;
    }
    //there was an error, so figure out what it was 
    //and print out relevant error message
    switch(errno){
        //directoryName is a file, not a directory
        case ENOTDIR:
            printf("%s is not a directory\n", directoryName);
            break;
        //no permission
        case EACCES:
            printf("You do not have permission to view %s\n", directoryName);
            break;
        //directory name empty or directory doesn't exist
        case ENOENT:
            printf("%s does not exist\n", directoryName);
            break;
        //generic error message
        default:
            printf("Could not open %s\n", directoryName);
            break;
    }
    //free memory if we allocated it
    if(didAllocateDirectoryName){
        free(directoryName);
    }
    //return 1 since there was an error
    return 1;
}

/**************************************************
* Linked list for background processes functions
***************************************************/

//node in linked list - stores background process ids
//has pointers to both next and previous so processes that finish
//can be easily removed
struct BackgroundProcessNode{
    pid_t processId;
    struct BackgroundProcessNode *previous;
    struct BackgroundProcessNode *next;
};

//linked list to store process ids of background processes
//works like a stack, with new background pids added to the front
struct BackgroundProcessList{
  struct BackgroundProcessNode *head;
};

//initialize linked list with null for first item
//since it is empty
void initializeBackgroundProcessList(struct BackgroundProcessList *backgroundProcessList){
    backgroundProcessList->head = NULL;
}

//adds pid to front of list
void addToBackgroundProcessList(pid_t pid, struct BackgroundProcessList *backgroundProcessList){
    //allocate memory
    struct BackgroundProcessNode *node = malloc(sizeof(struct BackgroundProcessNode));
    //check it succeeded
    assert(node != NULL);
    //save pid
    node->processId = pid;
    //will be first item, so previous is null
    node->previous = NULL;
    //set next to null, will be changed if there should be something next
    node->next = NULL;
    //insert into list
    //if head is null, it means it is empty, so just set head
    //otherwise set the previous head's previous pointer to the new head of the list
    //and the new node's next pointer to 
    if(backgroundProcessList->head != NULL){
        backgroundProcessList->head->previous = node;
        node->next = backgroundProcessList->head;
    }
    //set new head of the list
    backgroundProcessList->head = node;
}

//remove node from the list
//used when background process completes
void removeFromBackgroundProcessList(struct BackgroundProcessNode *node, struct BackgroundProcessList *backgroundProcessList){
    //check if node is head of list, since then don't need to reassign previous
    //and need to reassign head
    if(node == backgroundProcessList->head){
        backgroundProcessList->head = node->next;
    }
    //need to reassign previous node's next pointer
    else{
        node->previous->next = node->next;
    }
    //check to see if there is next node, and if so reassign it
    //to the previous node
    if(node->next != NULL){
        node->next->previous = node->previous;
    }
    //free memory from node
    free(node);
}


/*************************************
* Execute arbitrary command functions
**************************************/

////////////////////////////////////////
//Parse Argument functions
////////////////////////////////////////

//parses commands and arguments in commandLineBuffer and splits into array at whitespace
//commandArguments should be empty array where return value is stored and last item is NULL
//allocates memory for strings in commandArguments so should be destroyed afterwards
//commandLineBuffer is altered by strtok
//returns length of commandArguments array
int parseCommandArguments(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], char *commandArguments[MAX_ARGUMENT_COUNT + 1]){
    //pointer used by strtok_r to store position in string
    char *save;
    //initialize variable to hold current position in array
    int i = 0;
    //initialize strtok with commandLineBuffer
    char *currentWord = strtok_r(commandLineBuffer, " ", &save);

    //continue until no more arguments in buffer or we have reached argument count limit
    while( i < MAX_ARGUMENT_COUNT && currentWord != NULL){
        //need to allocate and copy space for word or it will
        //be cleaned up once we exit function
        char *savedWord = malloc(strlen(currentWord) + 1);
        //sanity check for malloc
        assert(savedWord != NULL);
        //copy currentWord so it can be saved
        strcpy(savedWord, currentWord);

        commandArguments[i] = savedWord;
        i++;
        //after the first time, call strtok with NULL
        currentWord = strtok_r(NULL, " ", &save);
    }
    //make sure last item is NULL
    commandArguments[i] = NULL;
    return i;
}

//checks arguments for "token <filename>" and returns the string matching "<filename>"
//also replaces "token" and "<filename>" with NULL in arguments so they will not be executed
//returns NULL if there is no redirection
//'>' is token for output redirection, '<' is token for input redirection
char * parseRedirection(char *commandArguments[MAX_ARGUMENT_COUNT + 1], char *token){
    //if last character was token the next string is the filename
    BOOL previousArgWasToken = FALSE;
    //iterate through arguments until we reach null, since that indicates the end of the arguments
    int i = 0;
    char *currentArgument;
    while((currentArgument = commandArguments[i]) != NULL){
        //if previous argument was token this should be the redirection filename
        if(previousArgWasToken == TRUE){
            //replace value in array with NULL, so won't be executed
            commandArguments[i] = NULL;
            return currentArgument;
        }
        //if token, free string and replace with null
        //and set flag
        else if(strcmp(currentArgument, token) == 0){
            previousArgWasToken = TRUE;
            //erase from arguments so it won't be executed
            commandArguments[i] = NULL;
            //free memory allocated for ">"
            free(currentArgument);
        }
        i++;
    }
    //if we're here there is no output redirection (or redirection symbol but no filename), so return NULL
    return NULL;
}

//free space allocated for arguments in commandArguments array
void destroyCommandArguments(char *commandArguments[MAX_ARGUMENT_COUNT + 1], int argumentCount){
    //initialize variables to hold current array index and current argument to be freed
    int i = 0;
    char *currentArgument;
    //free all arguments until we reach null or we have gone past end of array
    while(i < argumentCount && (currentArgument = commandArguments[i]) != NULL ){
        //free memory
        free(currentArgument);
        i++;
    }
}

//takes command in commandLineBuffer and modifies it in-place to expand
//$$ in the command to the current process id 
void expandVariables(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
    //create empty string to store commandLineBuffer with expanded pid
    char commandLineBufferExpanded[COMMAND_LINE_MAX_LENGTH];
    //initialize with null chars
    bzero(commandLineBufferExpanded, COMMAND_LINE_MAX_LENGTH);
    //initialize variables to store the index in the array for the source and destination index
    int sourceIndex;
    //need separate index for the destination string because as we expand $$, it will be potentially be
    //longer than the source, and thus the indexes will be out of sync
    int destIndex = 0;
    //keep track if previous character was a dollar sign, because if we then see a second one we know to expand
    BOOL previousCharWasDollarSign = FALSE;
    //convert pid to string
    //based on: http://stackoverflow.com/questions/15262315/how-to-convert-pid-t-to-string
    char pidString[COMMAND_LINE_MAX_LENGTH];
    sprintf(pidString, "%ld", (long)getpid());
    int pidStringLength = strlen(pidString);

    //copy all the chars from commandLineBuffer into expanded version, while checking for $$ and expanding it to pid
    for(sourceIndex = 0; sourceIndex < bufferLength && destIndex < COMMAND_LINE_MAX_LENGTH - 1; ++sourceIndex){
        char currentChar = commandLineBuffer[sourceIndex];
        //if the current character is '$' and the previous character was '$', we know we need to expand it
        if(currentChar == '$' && previousCharWasDollarSign == TRUE){
            //we need to erase the previous dollar sign, so back to previous index
            destIndex--;
            int pidStringIndex;
            //copy pid string to expanded variables, overwriting the previous $ that we have already written
            for(pidStringIndex = 0; pidStringIndex < pidStringLength; ++pidStringIndex){
                char pidChar = pidString[pidStringIndex];
                commandLineBufferExpanded[destIndex] = pidChar;
                //increase destination index, since we added a character
                destIndex++;
            }
            //reset if previous char was dollar sign, because we need to be able to expand
            //$$$$ to pidpid
            previousCharWasDollarSign = FALSE;
        }
        //no consecutive dollar sign, so no expanding variables
        else{
            //see if we are seeing dollar sign for the first time
            if(currentChar == '$'){
                previousCharWasDollarSign = TRUE;
            }
            //otherwise reset it
            else{
                previousCharWasDollarSign = FALSE;
            }
            //no variables to expand, so simply copy current character
            //and increment the destination index, since we have written in it
            commandLineBufferExpanded[destIndex] = currentChar;
            destIndex++;
        }
    }
    //overwrite commandLineBuffer with expanded version
    //if the expanded version ends up being longer than the max length it will be truncated
    strncpy(commandLineBuffer, commandLineBufferExpanded, COMMAND_LINE_MAX_LENGTH);
}


/////////////////////////////////////////////////////
// Parse for command for background process and input
//output redirection
/////////////////////////////////////////////////////

//determines if command ends with '&' so that command should run in background
//also erases ending '&' if there is one with null chars so it will not be executed
BOOL shouldExecuteInBackground(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
    int i;
    //start at end of command, ignoring whitespace to find first non-whitespace character
    //if that character is '&' delete it and return true, otherwise return false
    for (i = bufferLength -1; i >= 0; --i){
        //ignore trailing whitespace
        if(isspace(commandLineBuffer[i])){
            continue;
        }
        //'&' found at end of the string
        if(commandLineBuffer[i] == '&'){
            //erase character from string
            commandLineBuffer[i] = '\0';
            return TRUE;
        }
        //first non-whitespace character was found and it is not '&'
        else{
            break;
        }
    }
    //'&' not found
    return FALSE;
}



///////////////////////////////////////////////////
// Child and parent process functions
//////////////////////////////////////////////////

//used in child process to redirect output
void redirectOutput(char *commandArguments[MAX_ARGUMENT_COUNT + 1], BOOL isBackgroundCommand){
    //get output redirection if there is any
    char *outputFileName = parseRedirection(commandArguments, ">");
    //need to keep track if outputFileName is allocated, so we know if we have to free it
    //won't be allocated if we manually set to /dev/null
    BOOL isOutputFileNameAllocated = TRUE;
    //background commands with no output redirection get sent to /dev/null
    //based on: http://stackoverflow.com/questions/14846768/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect
    if(outputFileName == NULL && isBackgroundCommand == TRUE){
        outputFileName = "/dev/null";
        //set flag to false, so we don't try to free this later
        isOutputFileNameAllocated = FALSE;
    }
    
    //check for output redirection
    if(outputFileName != NULL){
        //based on Lecture 12 slides
        //attempt to redirect standard output to filename
        int fileDescriptor = open(outputFileName, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        //check that we were able to open the file
        //-1 means there was an error trying to do this
        if(fileDescriptor == -1){
            printf("cannot open %s for output\n", outputFileName);
            exit(1);
        }
        //redirect standard output '1' to the file we just opened
        fileDescriptor = dup2(fileDescriptor, 1);
        //check again for errors
        if(fileDescriptor == -1){
            printf("error redirecting standard output to %s\n", outputFileName);
            exit(1);
        }
    }
    //free outputFileName, if it was allocated
    //need to check if null and allocated because if we manually set it to "/dev/null"
    //it won't be allocated, or if no redirection was given it won't be allocated
    //since we don't need it anymore
    if(outputFileName != NULL && isOutputFileNameAllocated == TRUE){
        free(outputFileName);
    }
}

//used in child process to redirect standard input
void redirectInput(char *commandArguments[MAX_ARGUMENT_COUNT + 1], BOOL isBackgroundCommand){
    //get input redirection if there is any
    char *inputFileName = parseRedirection(commandArguments, "<");
    //need to keep track if inputFileName is allocated, so we know if we have to free it
    //won't be allocated if we manually set to /dev/null
    BOOL isInputFileNameAllocated = TRUE;
    //background commands with no input redirection get sent to /dev/null
    //based on: http://stackoverflow.com/questions/14846768/in-c-how-do-i-redirect-stdout-fileno-to-dev-null-using-dup2-and-then-redirect
    if(inputFileName == NULL && isBackgroundCommand == TRUE){
        inputFileName = "/dev/null";
        //set flag to false, so we don't try to free this later
        isInputFileNameAllocated = FALSE;
    }
    
    //check for input redirection
    if(inputFileName != NULL){
        //based on Lecture 12 slides
        //attempt to redirect standard input to filename
        int fileDescriptor = open(inputFileName, O_RDONLY);
        //check that we were able to open the file
        //-1 means there was an error trying to do this
        if(fileDescriptor == -1){
            printf("cannot open %s for input\n", inputFileName);
            exit(1);
        }
        //redirect standard input '0' to the file we just opened
        fileDescriptor = dup2(fileDescriptor, 0);
        //check again for errors
        if(fileDescriptor == -1){
            printf("error redirecting standard input to %s\n", inputFileName);
            exit(1);
        }
    }
    //free inputFileName, if it was allocated
    //need to check if null and allocated because if we manually set it to "/dev/null"
    //it won't be allocated, or if no redirection was given it won't be allocated
    //since we don't need it anymore
    if(inputFileName != NULL && isInputFileNameAllocated == TRUE){
        free(inputFileName);
    }
}

//prints error status if command fails, and is not a background command
void printExecutionError(int errorCode, char *commandName, BOOL isBackgroundCommand){
    //don't printout errors for background commands
    if(isBackgroundCommand == TRUE){
        return;
    }
    //print error based on errno
    //based on: https://linux.die.net/man/2/execve
    switch(errorCode){
        //permissions error for command
        case EACCES:
            printf("%s: execute permission denied\n", commandName);
            break;
        //file doesn't exist
        case ENOENT:
            printf("%s: no such file or directory\n", commandName);
            break;
        //problem with the executable file
        case ENOEXEC:
            printf("%s: could not be executed\n", commandName);
            break;
        //unspecified error
        default:
            printf("There was an error with trying to run %s\n", commandName);
            break;
    }

}

//Executes parses command in commandLineBuffer and executes in foreground for child process
void childProcessExecuteCommand(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength, BOOL isBackgroundCommand){
    //expand all '$$'' to pid in commandLineBuffec
    expandVariables(commandLineBuffer, bufferLength);

    //initialize variable to store commands in commandLineBuffer parsed into array
    //need space for 1 more than max arguments because we need to store NULL at the end
    char *commandArguments[MAX_ARGUMENT_COUNT + 1];
    int argumentCount = parseCommandArguments(commandLineBuffer, commandArguments);
    //only run command if there is a command to be run
    if(argumentCount < 1){
        //no commands (commandLineBuffer was empty of just whitespace), so exit early
        exit(0);
    }
    //redirect standard output as necessary
    redirectOutput(commandArguments, isBackgroundCommand);
    //redirect standard input as necessary
    redirectInput(commandArguments, isBackgroundCommand);

    //first item is commandArguments is program name, and we need to pass it again in the arguments
    int status = execvp(commandArguments[0], commandArguments);
    //check for error and normalize status to be either 1 for error
    //or 0 for finished successfully
    if(status == -1){
        status = 1;
        //error status stored in errno, so printout message based on this if not background command
        //(we have already checked that commandArguments has at least length one, so no need to check first)
        printExecutionError(errno, commandArguments[0], isBackgroundCommand);
    }
    else{
        status = 0;
    }
    //free memory allocated for commandArguments
    destroyCommandArguments(commandArguments, argumentCount);
    //close process after exec completes
    exit(status);
}

//action that parent takes while child process is executing command
//involves either waiting for child process to finish executing in the foreground, or 
//adding background process to list of background processes if it is to execute in background
//returns status code from child in foreground after finishes executing, or 0 if child is started in background
//childProcessId is child process id from fork()
int parentProcessExecuteCommand(pid_t childProcessId, struct BackgroundProcessList *backgroundProcessList, BOOL isBackgroundCommand){
    //run command in foreground, so wait for it to finish
    if(isBackgroundCommand == FALSE){ 
        //create variable to store return value from child process
        int status = 0;
        //set global foregroundPid so interrupt (control-c) will end it
        foregroundPid = childProcessId;
        waitpid(childProcessId, &status, 0);
        //clear foregroundPid, since the process has finished
        //foregroundPid = -1;
        //if there was an error calling waitpid
        //return value will be -1, however this will also happen if foreground process is interrupted,
        //so there is no need to check this, as we will just get false positives


        //check if process interrupted
        //if so, status is meaningless, since signal number will be used in status
        if(foregroundInterrupted == TRUE){
            //0 is meaningless, since signal number will be used
            printStatus(0);
        }
        //examine status - 0 means success, other values mean there was an error or 
        //process was interrupted
        //so normalize it for return value
        else if(status != 0){
            status = 1;
        }
        return status;
    }
    //command is background process
    //so just add to background processes and return immediately
    else{
        //print pid of child process
        //http://stackoverflow.com/questions/20533606/what-is-the-correct-printf-specifier-for-printing-pid-t
        printf("background pid is %ld\n", (long) childProcessId);
        addToBackgroundProcessList(childProcessId, backgroundProcessList);
        return 0;
    }
}


////////////////////////////////////////
// Main command execution function
////////////////////////////////////////

//creates a separate process to execute command given in commandLineBuffer and then
//executes the command
//return 0 if process succeeded, or 1 if it doesn't
//based on: https://support.sas.com/documentation/onlinedoc/sasc/doc/lr2/waitpid.htm
int executeCommand(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength, struct BackgroundProcessList *backgroundProcessList){
    //find out if command should be executed in background
    BOOL isBackgroundCommand = shouldExecuteInBackground(commandLineBuffer, bufferLength);
    //create new child process to execute command
    pid_t processId = fork();

    //branch based on process id so parent and child do different things:
    //child executes command and parent waits for it if it is in the foreground or
    //adds to background processes if it is in the background
    switch(processId){
        //error with fork
        case -1:
            printf("Could not create new process to execute %s\n", commandLineBuffer);
            return 1;
            break;
        //child process executing command
        case 0:
            childProcessExecuteCommand(commandLineBuffer, bufferLength, isBackgroundCommand);
            //should not reach here, because childProcessExecuteCommand exits, but we need return
            //statement so compiler doesn't complain
            return 0;
            break;
        //parent process
        //wait for child to finish executing (if done in foreground) and return result
        //otherwise just add to background processes and return 0
        default:
            return parentProcessExecuteCommand(processId, backgroundProcessList, isBackgroundCommand);
            break;
    }
}


///////////////////////////////////////////////////////////
// Background process commands
///////////////////////////////////////////////////////////

//returns true if process has either exited normally or topped by signal
//false otherwise
//status is the int passed in from waitpid
//based on: https://linux.die.net/man/2/waitpid
BOOL hasProcessStopped(pid_t childProcessId, pid_t waitpidResult, int status){
    //wait pid result will be 0 if no status is available
    if(waitpidResult == 0){
        return FALSE;
    }
    if(WIFEXITED(status) || WIFSIGNALED(status)){
        return TRUE;
    }
    return FALSE;
}

//prints out status of completed background processes
//and removes completed background processes from the list
void printBackgroundProcessStatus(struct BackgroundProcessList *backgroundProcessList){
    struct BackgroundProcessNode *node = backgroundProcessList->head;
    //initialize variable for status information in waitpid
    int status = 0;
    //iterate through all background processes, stopping them and freeing memory from the list
    while(node != NULL){
        //check process to see if still running
        pid_t waitpidResult = waitpid(node->processId, &status, WNOHANG);
        //don't do anything if process is still running
        if(!hasProcessStopped(node->processId, waitpidResult, status)){
            //process still running, continue with next node
            node = node->next;
            continue;
        }
        //print out exit status of completed process in format
        //background pid 4923 is done: exit value 0
        //or
        //background pid 4941 is done: terminated by signal 15
        //based on: https://linux.die.net/man/3/waitpid
        //check for exiting normally
        if(WIFEXITED(status)){
            printf("background pid %ld is done: exit value %d\n", (long) node->processId, WEXITSTATUS(status));
        }
        //otherwise killed by signal
        else{
            printf("background pid %ld is done: terminated by signal %d\n", (long) node->processId, WTERMSIG(status));
        }


        //remove completed process from the list
        //duplicate node, so we can store pointer to next node
        //before deleting current node
        struct BackgroundProcessNode *garbage = node;
        node = node->next;
        //free memory for current node
        removeFromBackgroundProcessList(garbage, backgroundProcessList);
    }
}


//kill all background processes
//and free memory from background process list
//called before program exits
void cleanUpBackgroundProcesses(struct BackgroundProcessList *backgroundProcessList){
    struct BackgroundProcessNode *node = backgroundProcessList->head;
    //initialize variable for status information in waitpid
    int status = 0;
    //iterate through all background processes, stopping them and freeing memory from the list
    while(node != NULL){
        //check process to see if still running
        pid_t waitpidResult = waitpid(node->processId, &status, WNOHANG);
        //kill background process if still running
        //based on: http://stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process
        if(!hasProcessStopped(node->processId, waitpidResult, status)){
            //send kill signal
            kill(node->processId, SIGKILL);
        }

        //duplicate node, so we can store pointer to next node
        //before deleting current node
        struct BackgroundProcessNode *garbage = node;
        node = node->next;
        //free memory for current node
        removeFromBackgroundProcessList(garbage, backgroundProcessList);
    }
}

/**
* Main function
*/
int main(int argc, char const *argv[]){
    //initialize interrupt (control-c) handler
    initializeInterruptHandler();

    //initialize variable to hold user input
    char commandLineBuffer[COMMAND_LINE_MAX_LENGTH];
    //initialize variable to hold return status code from running a command
    int returnStatusCode = 0;
    //initialize list to hold background process information
    struct BackgroundProcessList backgroundProcessList;
    initializeBackgroundProcessList(&backgroundProcessList);
	//main loop to get user input and execute commands
    //loops until user types 'exit' to exit shell
    while(1){
        //check status of background processes and print their status if they have completed
        //need to be first instead of last, so background status can be printed after blank lines
        //or comments
        printBackgroundProcessStatus(&backgroundProcessList);

    	//write user prompt
    	writePrompt();
        //get user input for command
        getUserInput(commandLineBuffer);
        
        //cache string length here, since we will be using it multiple places
        //to parse command
        int bufferLength = strlen(commandLineBuffer);

        //check if line is empty or a comment
        //(lines that begin with # are considered comments)
        if(bufferLength == 0 || commandLineBuffer[0] == COMMENT_CHAR){
        	//line is a comment or empty, so don't do anything
        	continue;
        }
        //check for 'exit' command to exit
        else if(strcmp(commandLineBuffer, "exit") == 0){
            break;
        }
        //check for 'status' command to print status
        else if(strcmp(commandLineBuffer, "status") == 0){
            returnStatusCode = printStatus(returnStatusCode);
            //built in commands reset foreground pid
            //so printStatus works correctly
            foregroundPid = NULL_FOREGROUND_PID;
            //also reset process interrupted, since built-in commands can't be interrupted
            foregroundInterrupted = FALSE;
        }
        else if(isCommandCD(commandLineBuffer, bufferLength) == 1){
            returnStatusCode = executeCD(commandLineBuffer, bufferLength);
            //built in commands reset foreground pid
            //so printStatus works correctly
            foregroundPid = NULL_FOREGROUND_PID;
            //also reset process interrupted, since built-in commands can't be interrupted
            foregroundInterrupted = FALSE;
        }
        else{
            //reset foreground interrupted, since nothing has happed yet, so can't be interrupted
            foregroundInterrupted = FALSE;
            //if we're here, we are executing user command
            returnStatusCode = executeCommand(commandLineBuffer, bufferLength, &backgroundProcessList);
        }
    }

    //if we're here, user entered 'exit'
    //kill all background child processes
    //and free memory from list
    //don't need to worry about foreground process, since if we are here, there isn't one currently running
    cleanUpBackgroundProcesses(&backgroundProcessList);


	return 0;
}