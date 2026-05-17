/*

TODO: correct paths like aa/bbb/file.com (should be file.com only)

*/

#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>


#include <sys/socket.h>

#include "commands.h"

#define IOPORT "/dev/ttyS0"
#define BAUDRATE B38400

#define SRC 4
#define DST 2
#define CMND 6

#define SECTORSIZE	128
#define WAIT		100000

const unsigned char term83 = 0x83;
const unsigned char term97 = 0x97;

int fd;
int rxFlag = 0;
int pos = 0;
int verbose = 0;

struct {
	unsigned char H, F, A;
	unsigned char FCB [37];
} TxData, RxData;

FILE *infile;
unsigned short i, start, end, run, current;
unsigned short int idx;
unsigned char checksum [2];
unsigned char buf [1024];
unsigned char Sector [SECTORSIZE];

unsigned short int studentNo = 0;

fd_set fdSet;
struct timeval timeout;
struct stat stat_p;

void Poke (unsigned int addr, unsigned char byte);
void SendBody ();
void SendBuf (unsigned short int len);
void SendChar (unsigned char ch);
void SendChecksum ();
void UpdateChecksum (unsigned char ch);
void Send ();
void List ();
void Run ();
int waitRx();

extern void SendPacket (int fd, int srcAddr, int dstAddr, int cmdType, unsigned char *buf, int len);
void signal_handler_IO (int status);
int ValidPkt ();

int main(int argc, char *argv []) {

	struct termios oldtio ,newtio;
	struct sigaction saio;
	unsigned char ch;
	unsigned int i, n;
	int enable = 1;
	int argNo = 1;
	int sectNo = 0;
	int retWait;
	char portBuf[32] = IOPORT;
	char* port = portBuf;

	char _CPM[] = {0xF0,0,0x7F,1,0,0,0x48,0,0,0,5,0,0x20,0,0x5f,0,0x63,0,0x70,0,0x6d,0,1,1,0x3f,0x83 };

    if (argc < 3) {
		printf ("Usage: ncopy [-port <e.g. com2 or /dev/ttyS1>] <Student No.> <File> [-verbose]\n");
		exit (1);
    }

	if (strcmp("-port", argv[1]) == 0)
	{
		argNo = 3;
		if (argv[2][0] == '/')
		{
			port = argv[2];
		}
		else
		{
			strcpy(portBuf+5, argv[2]);
		}
	}

    fd = open (port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd == -1) {
       printf ("Error: cannot open %s\n", port);
       exit (1);
    }

    studentNo = atoi (argv [argNo]);
	// "127" means "to all"
	if (studentNo == 0) studentNo = 127;

    argNo++;

	if (argc > (argNo+1) && strcmp("-verbose", argv[argNo+1]) == 0)
	{
		verbose = 1;
	}

/*
    // install the signal handler before making the device asynchronous
    saio.sa_handler = signal_handler_IO;
    saio.sa_mask  = 0;
    sigsetmask (0);
    saio.sa_flags = 0;
    saio.sa_restorer = NULL;
    sigaction (SIGIO, &saio, NULL);

    // allow the process to receive SIGIO
    fcntl(fd, F_SETOWN, getpid());
*/

    /* Make the file descriptor asynchronous (the manual page says only
       O_APPEND and O_NONBLOCK, will work with F_SETFL...) */
    fcntl(fd, F_SETFL, FASYNC);

    tcgetattr (fd, &oldtio);

    bzero (&newtio, sizeof (newtio));
    newtio.c_cflag = BAUDRATE | CS8 | PARENB | CLOCAL | CREAD;
    newtio.c_iflag = 0;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc [VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc [VMIN]     = 1;   /* blocking read until 1 chars received */

  	// the following 2 lines are ESSENTIAL for the code to work on CYGWIN
    cfsetispeed(&newtio, BAUDRATE);
	cfsetospeed(&newtio, BAUDRATE);

    tcflush (fd, TCIFLUSH);
    tcsetattr (fd, TCSANOW, &newtio);

    // *************************

	if (strcmp("_CPM", argv[argNo]) == 0)
	{
		// Send ping
		printf ("\nSending PING.");
		do {
		  SendPacket (fd, 0, studentNo, PING, NULL, 0);
		} while (! waitRx());
		printf ("\nGot PONG.\n");

		// Send "_CPM" command
		write (fd, _CPM, sizeof(_CPM));
		waitRx();
		exit(0);
	}

	// check the size of file
	stat (argv [argNo], &stat_p);

    infile = fopen (argv [argNo], "rb");
    if (infile == NULL) {
		printf ("Error: cannot open %s\n", argv [argNo]);
		exit (1);
    }

    for (i = 0; i < 11; i++) TxData.FCB [i + 1] = ' ';
    TxData.FCB [0] = 8;

    // Let's copy the filename
    for (i = 0; i < 8; i++) {
		if (argv [argNo][i] == '.') {
			i = 8;
			break;
		}
		TxData.FCB [1 + i] = toupper(argv[argNo][i]);
    }

    // and extension
    n = strlen (argv [argNo]);
    for (i = 0; i < 4; i++) {
		TxData.FCB [12 - i] = toupper(argv [argNo][n - i]);
    }

    // Send ping
    printf ("\nSending PING.");
	do {
      SendPacket (fd, 0, studentNo, PING, NULL, 0);
	} while (! waitRx());
    printf ("\nGot PONG.\n");

	// Create file on the net disk
	do {
       SendPacket (fd, 0, studentNo, NET_CREATE_FILE, (unsigned char *)&TxData, sizeof (TxData));
	} while (! waitRx());
    if (verbose) printf ("\nGot re: NET_CREATE_FILE.\n");

    // Reading the file sector-by-sector and writing them onto the net disk

	printf("\nNumber of sectors to send: %llu\n", (unsigned long long) (stat_p.st_size / sizeof (Sector)) + (stat_p.st_size % sizeof (Sector)));

    while (!feof (infile))
    {
		if (fread (Sector, sizeof (Sector), 1, infile) == 0) break;

   		SendPacket (fd, 0, studentNo, NET_MASTER_DATA, Sector, SECTORSIZE);

		do {
	        SendPacket (fd, 0, studentNo, NET_WRITE_FILE, (unsigned char *)&RxData, sizeof (RxData));
		} while (! waitRx());

        if (verbose)
        {
			printf("\nSent sector No.%d", sectNo);
		}
		else
		{
			if ((sectNo+1) % 10 == 0)
				printf (". %d ", sectNo+1);
			else
				printf (".");

        	fflush(stdout);
		}

		sectNo++;
    }

	// close the file on the net disk
	do {
	    SendPacket (fd, 0, studentNo, NET_CLOSE_FILE, (unsigned char *)&RxData, sizeof (RxData));
	} while (! waitRx());

    close (fd);
    printf ("\nDone.\n");
}


void GetRxData (unsigned char *p) {
int i;

  RxData.H = ReadEscapedByte (&p [11]);
  RxData.F = ReadEscapedByte (&p [13]);
  RxData.A = ReadEscapedByte (&p [15]);
  if (verbose) printf ("\n *** H: %.2x F: %.2x A: %.2x FCB: ", RxData.H, RxData.F, RxData.A);
  for (i = 0; i < 37; i++) {
    RxData.FCB [i] = ReadEscapedByte (&buf [17 + i * 2]);
    if (verbose) printf (" %.2x", RxData.FCB [i]);
  }
}

void CheckPacket () {
int src, dst;
int valid = 0;
int i;

  // Header
  if ((buf [0] == 0xf0) || (buf [0] == 0x78) // sorry :)
	&& (buf [1] == 00)) {
	dst = buf [2];
	src = buf [4];
	switch (buf [3]) {

		int readEscWord;

		case BASE:
			switch (buf [6]) {
				case RE_NET_CREATE_FILE:
					if (verbose) printf ("\n *** RE_NET_CREATE_FILE from %d to %d", src, dst);
					readEscWord = ReadEscapedWord (&(buf [7]));
					if (verbose) printf ("\n *** Payload: %d bytes", readEscWord);
					GetRxData (buf);
					break;
				case RE_NET_CLOSE_FILE:
                    if (verbose) printf ("\n *** RE_NET_CLOSE_FILE from %d to %d", src, dst);
					readEscWord = ReadEscapedWord (&(buf [7]));
					if (verbose) printf ("\n *** Payload: %d bytes", readEscWord);
					GetRxData (buf);
					break;
				case RE_NET_WRITE_FILE:
                    if (verbose) printf ("\n *** RE_NET_WRITE_FILE from %d to %d", src, dst);
					readEscWord = ReadEscapedWord (&(buf [7]));
					if (verbose) printf ("\n *** Payload: %d bytes", readEscWord);
                    GetRxData (buf);
					break;
				default:
					if (verbose) printf ("\n *** Unknown BASE packet 0x%.2x.", buf [6]);
					break;
			}
			break;
		case PING:
			break;
		case ACK:
			if (verbose) printf ("\n *** ACK from %d to %d", src, dst);
			break;
		case PONG:
			if (verbose) printf ("\nPONG from %d to %d.", src, dst);
			break;
	}
	// printf ("\n");
  }
}

/***************************************************************************
* signal handler. sets wait_flag to FALSE, to indicate above loop that     *
* characters have been received.                                           *
***************************************************************************/
void signal_handler_IO (int status) {
int i, n;

	n = read (fd, &(buf [pos]), 1024 - pos);
	pos += n;

	// Debug
/*
	printf ("\n****************************************");
	printf ("\nRecvData: got %d bytes, pos=%d, last byte: 0x%.2x\n", n, pos, buf [pos]);
	for (i = 0; i < pos; i++)
	{
		printf ("%.2x ", buf [i]);
	}
	printf ("\n****************************************\n");
*/

	if (pos > 256)
	{
		pos = 0;
	}
	else {
		if ( (buf [pos - 1] == 0x83) || (buf [pos - 1] == 0x97)) {
			rxFlag = 1;
			CheckPacket ();
		}
	}

}

int ValidPkt () {
	return (0);
}

int waitRx()
{
	int readfdSet, count = 0;
	signal(SIGIO, SIG_IGN); // the important one.

    rxFlag = pos =  0;

	while(1)
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = WAIT;

		FD_ZERO(&fdSet);
		FD_SET(fd, &fdSet);

		readfdSet = select(fd + 1,
						&fdSet,
						(fd_set *) 0,
						(fd_set *) 0,
						&timeout);
		if (readfdSet < 0)
		{
			printf("Error in select()\n");
			exit(1);
		}
		if (readfdSet > 0)
		{
			signal_handler_IO(1);
			if ( (buf [pos - 1] == 0x83) || (buf [pos - 1] == 0x97))
			{
				break;
			}
		}
		count++;
		usleep(10000);
		if (count == 10)
		{
			if (verbose) printf("\nFailed after %d select()'s", count);
			return 0;
		}
	}
	if (verbose) printf("\nGot reply after %d select()'s", count);
	return 1;
}
