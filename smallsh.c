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
//for waitpid
#include <sys/wait.h>

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
	printf("exit value %d\n", returnStatusCode);
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
            //based on: http://stackoverflow.com/questions/21558937/i-do-not-understand-how-execlp-works-in-linux
            status = execlp(commandLineBuffer, commandLineBuffer, (char *)NULL);
            //check for error
            if(status == -1){
                status = 1;
            }
            else{
                status = 0;
            }
            //close process after exec completes
            exit(status);
            break;
        //parent process
        //wait for child to finish executing and return result
        default:
            endId = waitpid(processId, &status, 0);
            //check for error calling waitpid
            if(endId == -1){
                printf("Error calling waitpid for %s\n", commandLineBuffer);
                return 1;
            }
            //examine status - 0 means success, other values mean there was an error or 
            //process was interrupted
            //so normalize it for return value
            //https://support.sas.com/documentation/onlinedoc/sasc/doc/lr2/waitpid.htm#statInfo
            if(status != 0){
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