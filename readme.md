# smallsh

A simple shell interpreter written in C.

## Dependencies

* gcc 4.4.7 or higher
* A UNIX/Linux compatible operating system

## Getting Started

* Download or clone this repository
* `cd` into the project directory and type `make`
* Type `./smallsh` to start smallsh

## Using smallsh

### Syntax

* Commands should in the format `[program_name] <arguments> <input and or output redirection> <&>`
* Program names are found using the current user's `PATH` variable
* Optional input and or output redirection should occur after the program name and any arguments, and can be in either order (i.e. it doesn't matter if you place output redirection before input redirection)
* Input redirection is done by using the syntax `< input_filename` and output redirection is done using `> output_filename`
* Quoting is not supported, and so program names, arguments and filenames that contain whitespace are not supported
* Optionally, `&` can be placed at the end of a command to run that command in the background
* Lines that start with `#` are treating as comments, and the commands in them are ignored

### smallsh built-in commands

* `cd` - operates similarly to the bash version of this command
* `status` - prints the return value of the last run foreground command, or the signal number if that process was stopped by a signal
* `exit` - terminates all running background processes and exits smallsh

