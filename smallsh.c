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


/*************************************
* Get user input functions
**************************************/

//gets user input, and stores in commandLineBuffer, minus trailing newlines
void getUserInput(char commandLineBuffer[COMMAND_LINE_MAX_LENGTH]){
    //clear the buffer
    bzero(commandLineBuffer, COMMAND_LINE_MAX_LENGTH);
    //string is less than max, since we need to put newline at the end and null char at end
    fgets(commandLineBuffer, COMMAND_LINE_MAX_LENGTH - 1, stdin);
    //check to see if command ends in newline-if so remove it
    int length = strlen(commandLineBuffer);
    //start at last character (last character is technically \0), but not counted by strlen
    int currentIndex = length - 1;
    //replace all trailing newlines with null char
    while(commandLineBuffer[currentIndex] == '\n' && currentIndex >= 0){
        //remove newline
        commandLineBuffer[currentIndex] = '\0';
        currentIndex--;
    }
}

void writePrompt(){
	printf(": ");
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
        
        //check if line is empty or a comment
        //(lines that begin with # are considered comments)
        if(strlen(commandLineBuffer) > 0 && commandLineBuffer[0] == COMMENT_CHAR){
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
    }

    //if we're here, user entered 'exit'
    //kill all child processes
    //based on: http://stackoverflow.com/questions/6501522/how-to-kill-a-child-process-by-the-parent-process
    //kill(processId, SIGKILL);

	return 0;
}