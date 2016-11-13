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

//create custom bool class, since c99 is required for stdbool.h
typedef int BOOL;
//members of BOOL type
#define TRUE 1
#define FALSE 0


/*************************************
* Handling interrupts
**************************************/
//global variable storing the pid of the current foreground process
//needs to be global variable, because there is no other way for the interrupt
//handler to get access to this pid
//-1 represents there is no foregroundPid
pid_t foregroundPid;

//handles action for when user presses control-c when foreground process is running-
//it will kill that process and print a message saying so
//based on CS344 lecture 13 slides
void interruptHandler(int signalNum){
    //don't do anything if there is no foreground process running
    if(foregroundPid == -1){
        puts("No foreground process running");
        return;
    }
    //must be foreground process, so send interrupt signal
    //based on: http://stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process
    //and http://www.csl.mtu.edu/cs4411.ck/www/NOTES/signal/kill.html
    kill(foregroundPid, SIGINT);
    //reset foregroundPid, so we don't try to kill it multiple times
    foregroundPid = -1;
}

//called at the beginning of the program, it sets interruptHandler() to be called
//when user enters control-c
void initializeInterruptHandler(){
    //initialize foregroundPid to -1, because nothing should be happening now
    foregroundPid = -1;

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
    //check to see if status code indicates process was interrupted
    if(WTERMSIG(returnStatusCode) == SIGINT){
        //print message that we are terminating process, and print signalNum as well,
        //which is what is stored in returnStatusCode
        printf("Terminated by signal %d\n", returnStatusCode);
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

//free space allocated for arguments in commandArguments array
void destroyCommandArguments(char *commandArguments[MAX_ARGUMENT_COUNT + 1], int argumentCount){
    //initialize variables to hold current array index and current argument to be freed
    int i = 0;
    char *currentArgument;
    //free all arguments until we reach null or we have gone past end of array
    while(i < argumentCount && (currentArgument = commandArguments[i]) != NULL ){
        //free memory
        free(currentArgument);
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


//Executes parses command in commandLineBuffer and executes in foreground for child process
void childProcessExecuteCommand(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
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
    //first item is commandArguments is program name, and we need to pass it again in the arguments
    int status = execvp(commandArguments[0], commandArguments);
    //free memory allocated for commandArguments
    destroyCommandArguments(commandArguments, argumentCount);
    //check for error and normalize status to be either 1 for error
    //or 0 for finished successfully
    if(status == -1){
        status = 1;
    }
    else{
        status = 0;
    }
    //close process after exec completes
    exit(status);
}


//creates a separate process to execute command given in commandLineBuffer and then
//executes the command
//return 0 if process succeeded, or 1 if it doesn't
//based on: https://support.sas.com/documentation/onlinedoc/sasc/doc/lr2/waitpid.htm
int executeCommand(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH], int bufferLength){
    pid_t processId = fork();
    //create variable to store return value from child process, and return value from exec
    int status;
    //stores result from waitpid - can't declare variable in switch statement
    pid_t endId;
    switch(processId){
        //error with fork
        case -1:
            printf("Could not create new process to execute %s\n", commandLineBuffer);
            return 1;
            break;
        //child process executing command
        case 0:
            childProcessExecuteCommand(commandLineBuffer, bufferLength);
            //should not reach here, because childProcessExecuteCommand exits, but we need return
            //statement so compiler doesn't complain
            return status;
            break;
        //parent process
        //wait for child to finish executing and return result
        default:
            //set foregroundPid so interrupt (control-c) will end it
            foregroundPid = processId;
            endId = waitpid(processId, &status, 0);
            //clear foregroundPid, since the process has finished
            foregroundPid = -1;
            //if there was an error calling waitpid
            //endId will be -1, however this will also happen if foreground process is interrupted,
            //so there is no need to do anything here, as we will just get false positives


            //check status for signal
            //https://support.sas.com/documentation/onlinedoc/sasc/doc/lr2/waitpid.htm#statInfo
            //check if process stopped by interrupt
            //and print the status if so - status will be signal number in this case
            if(WTERMSIG(status) == SIGINT){
                printStatus(status);
            }
            //examine status - 0 means success, other values mean there was an error or 
            //process was interrupted
            //so normalize it for return value
            else if(status != 0){
                status = 1;
            }
            return status;
            break;
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
	//main loop to get user input and execute commands
    //loops until user types 'exit' to exit shell
    while(1){
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
        }
        else if(isCommandCD(commandLineBuffer, bufferLength) == 1){
            returnStatusCode = executeCD(commandLineBuffer, bufferLength);
        }
        else{
            returnStatusCode = executeCommand(commandLineBuffer, bufferLength);
        }
    }

    //if we're here, user entered 'exit'
    //kill all child processes
    //based on: http://stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process
    //kill(processId, SIGKILL);

	return 0;
}