#include	<stdio.h>
#include	<stdarg.h>
#include	<stdlib.h>

/*********************************************************************
*
* Function  : die
*
* Purpose   : Print an error emssage and terminate program execution.
*
* Inputs    : int exit_code - code for "exit"
*             char *format - format string (ala printf)
*             ... - variable arguments list ala printf
*
* Output    : specified message
*
* Returns   : specified exit code
*
* Example   : die(1,"Can't open file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void die(int exit_code,char *format,...)
{
	va_list	ap;

	va_start(ap,format);
	vfprintf(stderr,format,ap);
	va_end(ap);
	exit(exit_code);
} /* end of die */
