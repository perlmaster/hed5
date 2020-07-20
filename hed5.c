/*********************************************************************
*
* File      : hed5.c
*
* Author    : Barry Kimelman
*
* Created   : December 17, 2001
*
* Purpose   : Hexadecimal file editor.
*
*********************************************************************/

#include	<stdio.h>
#include	<fcntl.h>
#include	<curses.h>
#include	<sys/types.h>
#include	<sys/stat.h>
/***  #include	<varargs.h>   ***/
#include	<stdarg.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<errno.h>
#include	<ctype.h>
#include	<string.h>

#define	NE(s1,s2)	(strcmp(s1,s2) !=0)

#define	NEXT_BLOCK		'n'
#define	PREV_BLOCK		'p'
#define	QUIT			'q'
#define	BLOCK1			'1'
#define	LASTBLOCK		'$'
#define	BLOCKNUM		'#'
#define	OFFSET			'o'
#define	HELP			'?'
#define	WRITE_BLOCK		'w'
#define	CHANGE_BYTE		'c'
#define	SAVE_BLOCK		's'
#define	SCAN_FORWARD	'/'
#define	SCAN_BACKWARD	'\\'

static	char	*filename = NULL;
static	long	filesize = 0L , num_blocks = 0L;
static	long	block_bytes = 0L , current_file_offset = 0L;
static	int	input_fd = -1;
static unsigned char	*block_buffer;
static unsigned char	*temp_buffer;
static	struct stat	filestats;
static	WINDOW	*data_win  = NULL, *msg_win = NULL;
static	WINDOW	*status_win  = NULL;
static	int		num_lines , num_cols , blocksize , num_data_rows;
static	int		tty_num_rows , tty_num_cols;
static	int		num_pairs = 8 , max_data_pairs = 0 , num_pairs_bytes = 0;
static	int		num_pairs_block_bytes = 0;

static	int		opt_w = 0 , opt_d = 0;

static	FILE	*debug_fp = NULL;
static	char	debug_filename[100];

extern	int		optind , optopt , opterr;
extern	void	die() , quit();

/*********************************************************************
*
* Function  : debug_print
*
* Purpose   : Optionally write a formatted message to the debugging
*             logfile.
*
* Inputs    : va_alist - arguments comprising message
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : debug_print("Process the file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void debug_print(char *format,...)
{
	va_list ap;

	if ( debug_fp ) {
		va_start(ap,format);
		vfprintf(debug_fp,format,ap);
		fflush(debug_fp);
		va_end(ap);
	}
	return;
} /* end of debug_print */

/*********************************************************************
*
* Function  : message
*
* Purpose   : Display a message.
*
* Inputs    : va_alist - arguments comprising message
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : message("Process the file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void message(char *format,...)
{
	va_list ap;
	char string[100];

	va_start(ap,format);
	vsprintf(string,format,ap);
	va_end(ap);
	wclear(msg_win);
	box(msg_win,'|','-');
	wborder(msg_win,0,0,0,0,0,0,0,0);
	mvwaddstr(msg_win,1,1,string);
	wrefresh(msg_win);

	return;
} /* end of message */

/*********************************************************************
*
* Function  : system_error
*
* Purpose   : Display a system call failure message.
*
* Inputs    : va_alist - arguments comprising message
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : system_error("Process the file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void system_error(char *format,...)
{
      va_list ap;
      char	errmsg[256];
      int	errnum;

      va_start(ap,format);
      errnum = errno;
      vsprintf(errmsg,format,ap);
      sprintf(&errmsg[strlen(errmsg)]," : %s\n",strerror(errnum));
      errno = errnum;
      message("%s",errmsg);
      beep();
      wgetch(msg_win);

      va_end(ap);
	  return;
} /* end of system_error */

/*********************************************************************
*
* Function  : error_message
*
* Purpose   : Display an error message.
*
* Inputs    : va_alist - arguments comprising message
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : error_message("Process the file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void error_message(char *format,...)
{
	va_list ap;
	char string[100];

	va_start(ap,format);
	vsprintf(string,format,ap);
	va_end(ap);
	wclear(msg_win);
	box(msg_win,'|','-');
	wborder(msg_win,0,0,0,0,0,0,0,0);
	mvwaddstr(msg_win,1,1,string);
	waddstr(msg_win,". Press any key to continue : ");
	wrefresh(msg_win);
	beep();
	wgetch(msg_win);

	return;
} /* end of error_message */

/*********************************************************************
*
* Function  : status_message
*
* Purpose   : Display a status message.
*
* Inputs    : va_alist - arguments comprising message
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : status_message("Process the file %s\n",filename);
*
* Notes     : (none)
*
*********************************************************************/

void status_message(char *format,...)
{
	va_list ap;
	char string[100];

	va_start(ap,format);
	vsprintf(string,format,ap);
	va_end(ap);
	wclear(status_win);
	box(status_win,'|','-');
	wborder(status_win,0,0,0,0,0,0,0,0);
	mvwaddstr(status_win,1,1,string);
	wrefresh(status_win);

	return;
} /* end of status_message */

/*********************************************************************
*
* Function  : pad_string
*
* Purpose   : Pad a string
*
* Inputs    : char *string - string to be padded
*             int pad_length - final length of padded string
*             char pad_char - padding character
*
* Output    : (none)
*
* Returns   : (nothing)
*
* Example   : pad_string(text,30,'*');
*
* Notes     : (none)
*
*********************************************************************/

static char *pad_string(char *string, int pad_length, char padding_char)
{
	int	length , num_pad_chars;

	length = strlen(string);
	if ( length < pad_length ) {
		num_pad_chars = (pad_length - length) + 1;
		memset(&string[length],padding_char,num_pad_chars);
		string[pad_length] = '\0';
	}
	return(string);
} /* end of pad_string */

/*********************************************************************
*
* Function  : get_number
*
* Purpose   : Get a numeric value
*
* Inputs    : char *prompt - the input prompt
*
* Output    : (none)
*
* Returns   : long number - the numeric value
*
* Example   : count = get_number("Enter : ");
*
* Notes     : (none)
*
*********************************************************************/

static long get_number(prompt)
char *prompt;
{
	int	num_digits;
	long	number;
	char	digit , digits[100];

	message("%s",prompt);
	digit = (char)wgetch(msg_win);
	for ( num_digits = 0 ; !isspace(digit) ; ++num_digits ) {
		waddch(msg_win,digit);
		wrefresh(msg_win);
		digits[num_digits] = digit;
		digit = (char)wgetch(msg_win);
	} /* WHILE */
	digits[num_digits] = '\0';
	if ( digits[0] == 'x' ) {
		if ( sscanf(&digits[1],"%lx",&number) != 1 ) {
			error_message("bad hex data : %s",digits);
			number = 0;
		} /* IF */
	} /* IF */
	else {
		number = atol(digits);
	} /* ELSE */

	return(number);
} /* end of get_number */

/*********************************************************************
*
* Function  : get_string
*
* Purpose   : Get a string
*
* Inputs    : char *prompt - the input prompt
*             char *buffer - buffer to receive string
*
* Output    : (none)
*
* Returns   : pointer to string buffer
*
* Example   : get_string("Enter : ",string);
*
* Notes     : (none)
*
*********************************************************************/

static char *get_string(char *prompt, char *buffer)
{
	int	num_chars;
	char	ch;

	message("%s",prompt);
	ch = (char)wgetch(msg_win);
	for ( num_chars = 0 ; !isspace(ch) ; ++num_chars ) {
		waddch(msg_win,ch);
		wrefresh(msg_win);
		buffer[num_chars] = ch;
		ch = (char)wgetch(msg_win);
	} /* WHILE */
	buffer[num_chars] = '\0';

	return(buffer);
} /* end of get_number */

/*********************************************************************
*
* Function  : display_block
*
* Purpose   : Display a block of data
*
* Inputs    : (none)
*
* Output    : A hex/character dump of a block of data
*
* Returns   : (nothing)
*
* Example   : display_block();
*
* Notes     : This function displays the "current" block.
*
*********************************************************************/

static void display_block()
{
	long	offset , block_offset , block_start;
	unsigned char	line[100] , chunk[64] , *blockptr , ch;
	int	count , row , num_bytes , col1;

	if ( lseek(input_fd,current_file_offset,SEEK_SET) < 0 ) {
		system_error("Can't lseek to offset 0x%x",
				current_file_offset);
		return;
	}
	block_bytes = read(input_fd,block_buffer,blocksize);
	if ( block_bytes < 0L ) {
		system_error("Can't read block at offset 0x%x",
				current_file_offset);
		return;
	}
	status_message("File : %s, offset 0x%x, size = %ld (0x%x)",
		filename,current_file_offset,filesize,filesize);
	row = 1;
	block_offset = 0L;
	wclear(data_win);
	box(data_win,'|','-');
	wborder(data_win,0,0,0,0,0,0,0,0);
	col1 = 2;
	for ( row = 1 ; row <= num_data_rows && block_offset < block_bytes; ++row ) {
		offset = current_file_offset + block_offset;
		blockptr = &block_buffer[block_offset];
		sprintf((char *)line,"%08x : ",offset);
		num_bytes = 0;
		block_start = block_offset;
		for ( num_bytes = 0 ; num_bytes < num_pairs_bytes && offset < filesize ; ) {
			if ( offset+1L < filesize ) {
				sprintf((char *)chunk,"%02x%02x ",blockptr[0],
							blockptr[1]);
				num_bytes += 2;
			} /* IF */
			else {
				sprintf((char *)chunk,"%02x%-2s ",blockptr[0]," ");
				num_bytes += 1;
			} /* ELSE */
			blockptr += 2;
			offset += 2L;
			block_offset += 2L;
			strcat((char *)line,(char *)chunk);
		} /* FOR */
		pad_string((char *)line,11 + num_pairs_block_bytes,' ');
		sprintf((char *)chunk,"|%-*.*s|",num_pairs_bytes,num_pairs_bytes," ");
		strcat((char *)line,(char *)chunk);
		mvwaddstr(data_win,row,col1,(char *)line);
		wrefresh(data_win);
		blockptr = &block_buffer[block_start];
/***		col = col1 + 52; ***/
		sprintf((char *)line,"%-*.*s",num_pairs_bytes,num_pairs_bytes," ");
		for ( count = 0 ; count < num_bytes ; ++count ) {
			ch = *blockptr++;
			if ( ch < 0x20 || ch > 0x7e ) {
				line[count] = '.';
			} /* IF */
			else {
				line[count] = ch;
			} /* ELSE */
		} /* FOR loop over 1 line */
		mvwaddstr(data_win,row,col1 + 11 + num_pairs_block_bytes + 1,(char *)line);
	} /* FOR loop over all lines in block */
	wrefresh(data_win);

	return;
} /* end of display_block */

/*********************************************************************
*
* Function  : show_help
*
* Purpose   : Display a help screen.
*
* Inputs    : (none)
*
* Output    : The help screen
*
* Returns   : (nothing)
*
* Example   : show_help();
*
* Notes     : (none)
*
*********************************************************************/

static void show_help()
{
	int	row , col;
	char	buffer[256];

	wclear(data_win);
	box(data_win,'|','-');
	wborder(data_win,0,0,0,0,0,0,0,0);
	col = 2;
	row = 2;
	mvwaddstr(data_win,row++,col,"Available Commands :");
	col += 4;
	row += 1;
	mvwaddstr(data_win,row++,col,"q - quit");
	mvwaddstr(data_win,row++,col,"n - next block");
	mvwaddstr(data_win,row++,col,"p - previous block");
	mvwaddstr(data_win,row++,col,"1 - first block");
	mvwaddstr(data_win,row++,col,"$ - last block");
	mvwaddstr(data_win,row++,col,"# - goto specified block");
	mvwaddstr(data_win,row++,col,"o - goto specified offset");
	mvwaddstr(data_win,row++,col,
		"    (offset can be in decimal or hexadecimal)");
	mvwaddstr(data_win,row++,col,"w - write current block to a file");
	mvwaddstr(data_win,row++,col,"c - change a byte value");
	mvwaddstr(data_win,row++,col,"s - save current block back to file");
	mvwaddstr(data_win,row++,col,"/ - scan forward");
	mvwaddstr(data_win,row++,col,"\\ - scan backward");
	mvwaddstr(data_win,row++,col,"? - display this help summary");
	sprintf(buffer,"Rows : %d , Cols : %d",tty_num_rows,tty_num_cols);
	mvwaddstr(data_win,row++,col,buffer);
	col -= 4;
	wrefresh(data_win);
	message("Press any key to continue.");
	wgetch(msg_win);

	display_block();
	return;
} /* end of show_help */

/*********************************************************************
*
* Function  : write_current_block
*
* Purpose   : Write current block to a file.
*
* Inputs    : (none)
*
* Output    : Current block is written to a file.
*
* Returns   : (nothing)
*
* Example   : write_current_block();
*
* Notes     : (none)
*
*********************************************************************/

int write_current_block()
{
	char	filename[100];
	FILE	*output;

	get_string("Enter filename : ",filename);
	while ( NE(filename,"$") && access(filename,F_OK) == 0 ) {
		get_string("File exists, try again : ",filename);
	} /* WHILE */
	if ( NE(filename,"$") ) {
		output = fopen(filename,"w");
		if ( output == NULL ) {
			system_error("Can't open file \"%s\"",
					filename);
		} /* IF */
		else {
			if ( fwrite(block_buffer,block_bytes,1,output) != 1 ) {
				system_error("Write failed");
			} /* IF */
			fclose(output);
		} /* ELSE */
	} /* IF */

	return(0);
} /* end of write_current_block */

/*********************************************************************
*
* Function  : save_current_block
*
* Purpose   : Save current block data back to file.
*
* Inputs    : (none)
*
* Output    : Current block is written back to file.
*
* Returns   : (nothing)
*
* Example   : save_current_block();
*
* Notes     : (none)
*
*********************************************************************/

void save_current_block()
{
	if ( ! opt_w ) {
		message("Can't update a read-only file. Press any key to continue.");
		wgetch(msg_win);
		return;
	} /* IF */

	if ( lseek(input_fd,current_file_offset,SEEK_SET) < 0 ) {
		system_error("Can't lseek to offset 0x%x",
				current_file_offset);
		return;
	}
	if ( write(input_fd,block_buffer,block_bytes) != block_bytes ) {
		system_error("Write failed");
	} /* IF */
} /* end of save_current_block */

/*********************************************************************
*
* Function  : get_hex_byte
*
* Purpose   : Get a hexadecimal byte value from the user
*
* Inputs    : char *prompt - the input prompt
*
* Output    : (none)
*
* Returns   : long number - the numeric value
*
* Example   : count = get_hex_byte("Enter : ");
*
* Notes     : (none)
*
*********************************************************************/

static unsigned char get_hex_byte(char *prompt)
{
	int	num_digits;
	long	number;
	char	digit , digits[100];
	unsigned char	hex_byte;

	message("%s",prompt);
	digit = (char)wgetch(msg_win);
	for ( num_digits = 0 ; !isspace(digit) ; ++num_digits ) {
		waddch(msg_win,digit);
		wrefresh(msg_win);
		digits[num_digits] = digit;
		digit = (char)wgetch(msg_win);
	} /* WHILE */
	digits[num_digits] = '\0';
	if ( num_digits < 1 || num_digits > 2 ) {
		error_message("Bad number of hex digits");
		return('\0');
	} /* IF */
	if ( sscanf(digits,"%lx",&number) != 1 ) {
		error_message("bad hex data : %s",digits);
		hex_byte = 0;
	} /* IF */
	else {
		hex_byte = (unsigned char)(number & 0x000000ff);
	} /* ELSE */

	return(hex_byte);
} /* end of get_hex_byte */

/*********************************************************************
*
* Function  : change_block_byte
*
* Purpose   : Change the value of a single byte
*
* Inputs    : (none)
*
* Output    : (none)
*
* Returns   : zero
*
* Example   : change_block_byte();
*
* Notes     : (none)
*
*********************************************************************/

int change_block_byte()
{
	unsigned char	byte , prompt[100];
	long	file_offset , block_offset;

	file_offset = get_number("Enter file offset :");
	if ( file_offset < current_file_offset ||
		file_offset >= (current_file_offset+block_bytes) ) {
		error_message("Offset not in current block");
		return(1);
	} /* IF */
	sprintf((char *)prompt,"Enter hex value for byte at 0x%x:",
			file_offset);
	byte = get_hex_byte((char *)prompt);
	block_offset = file_offset - current_file_offset;
	block_buffer[block_offset] = byte;
	save_current_block();
	display_block();

	return(0);
} /* end of change_block_byte */

/*********************************************************************
*
* Function  : scan_block
*
* Purpose   : Scan forward for a string.
*
* Inputs    : unsigned char *block - file data buffer
*             int byte_count - byte count for file data buffer
*             char *string - search string
*
* Output    : (none)
*
* Returns   : zero
*
* Example   : scan_block();
*
* Notes     : (none)
*
*********************************************************************/

int scan_block(unsigned char *block, int byte_count, char *string)
{
	int		string_size , match , num_loops , count;
	unsigned char	*ptr1;

	match = 0;
	string_size = strlen(string);
	if ( string_size <= byte_count ) {
		num_loops = (byte_count - string_size) + 1;
		ptr1 = block;
		for ( count = 0 ; count < num_loops ; ++count , ++ptr1 ) {
			if ( memcmp(ptr1,string,string_size) == 0 ) {
				return(1);
			} /* IF */
		} /* FOR */
	} /* IF */

	return(match);
} /* end of scan_block */

/*********************************************************************
*
* Function  : scan_forward
*
* Purpose   : Scan forward for a string.
*
* Inputs    : (none)
*
* Output    : (none)
*
* Returns   : If found Then file offset Else -1L
*
* Example   : scan_forward();
*
* Notes     : (none)
*
*********************************************************************/

long scan_forward()
{
	char	string[200];
	int		num_bytes , match;
	long	offset;

	get_string("Enter string : ",string);

	match = 0;
	offset = current_file_offset;
	while ( 1 ) {
		if ( lseek(input_fd,offset,SEEK_SET) < 0 ) {
			system_error("Can't lseek to offset 0x%x", offset);
			break;
		} /* IF */
		num_bytes = read(input_fd,temp_buffer,blocksize);
		if ( num_bytes <= 0 ) {
			break;
		}
		if ( scan_block(temp_buffer,num_bytes,string) ) {
			match = 1;
			return(offset);
		} /* IF */
		offset += blocksize;
		if ( offset >= filesize ) {
			break;
		} /* IF */
	} /* WHILE */
	error_message("Not found");

	display_block();
	return(-1L);
} /* end of scan_forward */

/*********************************************************************
*
* Function  : scan_backward
*
* Purpose   : Scan forward for a string.
*
* Inputs    : (none)
*
* Output    : (none)
*
* Returns   : If found Then file offset Else -1L
*
* Example   : scan_backward();
*
* Notes     : (none)
*
*********************************************************************/

long scan_backward()
{
	char	string[200];
	int		num_bytes;
	long	offset;

	get_string("Enter string : ",string);

	offset = current_file_offset;
	debug_print("scan_backward() from offset 0x%x looking for '%s'\n",
					offset,string);
	while ( offset >= 0L ) {
		if ( lseek(input_fd,offset,SEEK_SET) < 0 ) {
			system_error("Can't lseek to offset 0x%x", offset);
			break;
		} /* IF */
		num_bytes = read(input_fd,temp_buffer,blocksize);
		if ( num_bytes <= 0 ) {
			break;
		}
		if ( scan_block(temp_buffer,num_bytes,string) ) {
			debug_print("Found it.\n");
			return(offset);
		} /* IF */
		offset -= blocksize;
		debug_print("not found, backup to offset 0x%x\n",offset);
	} /* WHILE */
	error_message("Not found");
	debug_print("Not found\n");

	display_block();
	return(-1L);
} /* end of scan_backward */

/*********************************************************************
*
* Function  : main
*
* Purpose   : Program entry point.
*
* Inputs    : int argc - number of arguments
*             char *argv[] - list of arguments
*
* Output    : (none)
*
* Returns   : 0 --> success , 1 --> error
*
* Example   : hed4 filename
*
* Notes     : (none)
*
*********************************************************************/

int main(int argc, char *argv[])
{
	char	command , *command_prompt , *ptr;
	long	block_num , longnum , offset;
	int		c , errflag , open_mode , row1;

	errflag = 0;
	while ( (c = getopt(argc,argv,":dwp:")) != -1 ) {
		switch (c) {
		case 'w':
			opt_w = 1;
			break;
		case 'd':
			opt_d = 1;
			break;
		case 'p':
			num_pairs = atoi(optarg);
			break;
		case '?':
			printf("Unknown option '%c'\n",optopt);
			errflag += 1;
			break;
		case ':':
			printf("Missing value for option '%c'\n",optopt);
			errflag += 1;
			break;
		default:
			printf("Unexpected value from getopt() '%c'\n",c);
		} /* SWITCH */
	} /* WHILE */

	if ( errflag || optind >= argc ) {
		die(1,"Usage : %s [-dw] [-p num_pairs] filename\n",argv[0]);
	} /* IF */

	filename = argv[optind];
	open_mode = opt_w ? O_RDWR : O_RDONLY;
	input_fd = open(filename,open_mode);
	if ( input_fd < 0 ) {
		quit(1,"Can't open file \"%s\"",filename);
	}
	if ( stat(filename,&filestats) < 0 ) {
		quit(1,"stat failed");
	} /* IF */
	filesize = filestats.st_size;
	current_file_offset = 0L;

	if ( opt_d ) {
		ptr = getenv("HOME");
		if ( ptr != NULL ) {
			sprintf(debug_filename,"%s/hed.log",ptr);
		} /* IF */
		else {
			sprintf(debug_filename,"/tmp/hed.log.%d",getpid());
		} /* ELSE */
		debug_fp = fopen(debug_filename,"w");
	} /* IF */
	else {
		debug_fp = NULL;
	} /* ELSE */

	initscr();	/* initialize curses */
	nonl();
	cbreak();
	noecho();
	tty_num_rows = tgetnum("li");
	tty_num_cols = tgetnum("co");
	if ( getenv("LINES") != NULL ) {
		num_lines = atoi(getenv("LINES"));
	} /* IF */
	else {
		num_lines = tty_num_rows;
	} /* ELSE */

	if ( getenv("COLUMNS") != NULL ) {
		num_cols = atoi(getenv("COLUMNS"));
	} /* IF */
	else {
		num_cols = tty_num_cols;
	} /* ELSE */

	max_data_pairs = ( (tty_num_cols - 13) / 7 ) - 1;
	if ( num_pairs > max_data_pairs ) {
		num_pairs = max_data_pairs;
	} /* IF too many requested columns */
	num_pairs_bytes = num_pairs * 2;
	num_pairs_block_bytes = num_pairs * 5;

	num_data_rows = num_lines - 8;
	blocksize = num_data_rows * num_pairs_bytes;
	num_blocks = (filesize + blocksize - 1) / blocksize;
	block_buffer = (unsigned char *)malloc(blocksize);
	if ( block_buffer == NULL ) {
		quit(1,"malloc failed");
	} /* IF */
	temp_buffer = (unsigned char *)malloc(blocksize);
	if ( temp_buffer == NULL ) {
		quit(1,"malloc failed");
	} /* IF */

	data_win = newwin(num_lines-6,num_cols,0,0);
	if ( data_win == NULL ) {
		clear();
		addstr("newwin failed for data window");
		refresh();
		getch();
		endwin();
		exit(1);
	}
	box(data_win,'|','-');
	wborder(data_win,0,0,0,0,0,0,0,0);
	mvwaddstr(data_win,1,1,"File data");
	wrefresh(data_win);

	row1 = num_lines - 6;
	msg_win = newwin(3,num_cols,row1,0);
	if ( msg_win == NULL ) {
		delwin(data_win);
		clear();
		addstr("newwin failed for message window");
		refresh();
		getch();
		endwin();
		exit(1);
	}
	row1 += 3;

	status_win = newwin(3,num_cols,row1,0);
	if ( status_win == NULL ) {
		delwin(data_win);
		delwin(msg_win);
		clear();
		addstr("newwin failed for status window");
		refresh();
		getch();
		endwin();
		exit(1);
	}
	row1 += 3;
	display_block();
	command_prompt = "Enter your command (q,n,p,1,$,#,o,w,c,s,/,\\,?) : ";
	message("%s",command_prompt);
	command = wgetch(msg_win);

	debug_print("Process file \"%s\"",debug_filename);
	debug_print(" , num_blocks = %ld , block_bytes = %ld\n",
			num_blocks,block_bytes);
	debug_print("blocksize = %d , 0x%x\n",blocksize,blocksize);

	while ( command != QUIT ) {
		switch ( command ) {
		case NEXT_BLOCK:
			if ( current_file_offset+blocksize >= filesize ) {
				error_message("Already on last block");
			} /* IF */
			else {
				current_file_offset += (long)blocksize;
				display_block();
			} /* ELSE */
			break;
		case PREV_BLOCK:
			if ( current_file_offset-blocksize < 0L ) {
				error_message("Already on 1st block");
			} /* IF */
			else {
				current_file_offset -= (long)blocksize;
				display_block();
			} /* ELSE */
			break;
		case BLOCK1:
			current_file_offset = 0L;
			display_block();
			break;
		case LASTBLOCK:
			current_file_offset = filesize - blocksize;
			current_file_offset = (num_blocks - 1L) * blocksize;
			if ( current_file_offset < 0L ) {
				current_file_offset = 0L;
			} /* IF */
			display_block();
			break;
		case BLOCKNUM:
			block_num = get_number("Enter block # : ");
			if ( block_num < 0L || block_num >= num_blocks ) {
				error_message("Invalid block number");
			} /* IF */
			else {
				current_file_offset = (long)(block_num * blocksize);
				display_block();
			} /* ELSE */
			break;
		case OFFSET:
			longnum = get_number("Enter offset : ");
			if ( longnum >= 0L && longnum < filesize ) {
				current_file_offset = longnum;
				display_block();
			} /* IF */
			else {
				error_message("Invalid file offset");
			} /* ELSE */
			break;
		case HELP:
			show_help();
			break;
		case WRITE_BLOCK:
			write_current_block();
			break;
		case SAVE_BLOCK:
			save_current_block();
			break;
		case CHANGE_BYTE:
			change_block_byte();
			break;
		case SCAN_FORWARD:
			offset = scan_forward();
			if ( offset >= 0L ) {
				current_file_offset = offset;
				display_block();
			} /* IF */
			break;
		case SCAN_BACKWARD:
			offset = scan_backward();
			if ( offset >= 0L ) {
				current_file_offset = offset;
				display_block();
			} /* IF */
			break;
		default:
			error_message("Invalid command [%c]",command);
		} /* SWITCH */
		message("%s",command_prompt);
		command = wgetch(msg_win);
	} /* WHILE */
	delwin(data_win);
	delwin(msg_win);
	clear();
	refresh();
	endwin();	/* terminate curses processing */
	exit(0);
} /* end of main */
