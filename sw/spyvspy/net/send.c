#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

// #include <signal.h>
// struct termios oldtio;
// struct sigaction saio;

#define IOPORT "/dev/ttyS0"
#define BAUDRATE B38400

#define SRC 4
#define DST 2
#define CMND 6
#define MAXBLKSIZE	56
#define SIZE16K 16*1024
#define SIZE32K 32*1024

FILE *infile;
unsigned short i, start, end, run, current;
unsigned short int idx;
unsigned char checksum [2];
unsigned char buf [MAXBLKSIZE];

int fd;
fd_set fdSet;
struct timeval timeout;
struct stat stat_p;
int romSize, rxFlag, pos, verbose=0, argNo = 1;

char portBuf[32] = IOPORT;
char* port = portBuf;

char* fileNamePointer[100];
int filesNumber = 0, fileIdx;

unsigned char ROM2BIN_startCode[] =
{
/*
	commented because this code section is needed only for
    disk drive switching off, but we run on a diskless MSX

	0xFB,				// ei
	0x76,				// halt
	0x10,0xFD,			// djnz [go to halt]
	0x3E,0xC9,			// ld a,C9
	0x32,0x9F,0xFD,		// ld (FD9F),a
*/
	// this code switches RAM page on address 0x4000,
	// copies 16K from 0x9000 to 0x4000
	// (or to 0x8000 when "ld de,4000" patched to be "ld de,8000")
	// and then execues ROM code
	// (or returns to Basic if "jp hl" is patched to be "nop")

	0xCD,0x38,0x01,		// call 0138
	0xE6,0x30,			// and 30
	0x0F,				// rrca
	0x0F,				// rrca
	0x0F,				// rrca
	0x0F,				// rrca
	0x4F,				// ld c,a
	0x06,0x00,			// ld b,00
	0x21,0xC5,0xFC,		// ld hl,FCC5
	0x09,				// add hl,bc
	0x7E,				// ld a,(hl)
	0xE6,0x30,			// and 30
	0x0F,				// rrca
	0x0F,				// rrca
	0xB1,				// or c
	0xF6,0x80,			// or 80
	0x26,0x40,			// ld h,40
	0xCD,0x24,0x00,		// call 0024
	0xF3,				// di
	0x11,0x00,0x40,		// ld de,4000 (0x40 will be patched to 0x80)
	0x21,0x00,0x90,		// ld hl,9000
	0x01,0x00,0x40,		// ld bc,4000
	0xED,0xB0,			// ldir
	0x2A,0x02,0x40,		// ld hl,(4002)
	0xE9,				// jp hl (can be patched to reach next command)

	0x3E, 0x80,			// ld a,80
	0x26, 0x40,			// ld h,40
	0xCD,0x24,0x00,		// call 0024
	0xC9				// ret
};

unsigned char pingPacket [] = { 0xf0, 0x00, 0x00, 0x05, 0x00, 0x83 };

unsigned char binBuf [SIZE32K]; // enough for any bloadable binary

unsigned char readBuf[1024];

unsigned short int studentNo = 0;

void Poke (unsigned int addr, unsigned char byte);
void SendHeader ();
void SendBuf (unsigned short int len);
void SendChar (unsigned char ch);
void SendChecksum ();
void UpdateChecksum (unsigned char ch);
void Send ();
void Run ();


/***************************************************************************
* signal handler. sets wait_flag to FALSE, to indicate above loop that     *
* characters have been received.                                           *
***************************************************************************/

int main(int argc, char *argv [])
{
	struct termios newtio;

	unsigned char ch;
	unsigned int i;

    if (argc < 3)
    {
		printf ("\nUsage:\n send [-port <e.g. com2 or /dev/ttyS1>] <student No. or zero> <file> [file ...] [-verbose]");
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
	if (fd == -1)
	{
	  printf ("\nError: cannot open port %s\n", port);
	  exit (1);
	}

    studentNo = atoi (argv [argNo]);
	// "127" means "to all"
	if (studentNo == 0) studentNo = 127;

	printf("\nPort: %s, student No.%d", port, studentNo);

    	argNo++;

	if (strcmp("-verbose", argv[argc-1]) == 0)
	{
		verbose = 1;
	}

	for (i=argNo; i < (argc-verbose); i++)
	{
		fileNamePointer[filesNumber] = argv[i];
		filesNumber++;
	}

    /* Make the file descriptor asynchronous (the manual page says only
       O_APPEND and O_NONBLOCK, will work with F_SETFL...) */
    fcntl(fd, F_SETFL, FASYNC);

//    tcgetattr (fd, &oldtio);

    bzero (&newtio, sizeof (newtio));
    newtio.c_cflag = BAUDRATE | CS8 | PARENB | CLOCAL | CREAD;
    newtio.c_iflag = 0;
    newtio.c_oflag = 0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag = 0;

    newtio.c_cc [VTIME]    = 0;   /* inter-character timer unused */
    newtio.c_cc [VMIN]     = 1;   /* blocking read until 1 chars received */

    cfsetispeed(&newtio, BAUDRATE);
	cfsetospeed(&newtio, BAUDRATE);

    tcflush (fd, TCIFLUSH);
    tcsetattr (fd, TCSANOW, &newtio);

	for (fileIdx = 0; fileIdx < filesNumber; fileIdx++)
	{
	    infile = fopen (fileNamePointer[fileIdx], "rb");
		if (infile == NULL)
		{
			printf ("\nError: cannot open file %s\n", fileNamePointer[fileIdx]);
			exit (1);
	    }

		// *************************

		fread (&ch, 1, 1, infile);

		if (ch == 0xfe)
		{
			// -----------------------------------------------------
			//
			//                      BINARY FILE
			//
			// -----------------------------------------------------
			unsigned short checksum;
			int i;

			fread (&start, 2, 1, infile);
			fread (&end, 2, 1, infile);
			fread (&run, 2, 1, infile);

			fread (&binBuf, end-start+1, 1, infile);
			printf ("\nFile: %s; Start: %x, End: %x, Run: %x", fileNamePointer[fileIdx], start, end, run);

			Send ();
			Run ();
			// this wait is convenient when sending MegaROMs divided into many .BINs
			usleep (1000000);
			printf ("\n Done.\n");

		}
		else if (ch == 0xff)
		{
			// -----------------------------------------------------
			//
			//                      BASIC
			//
			// -----------------------------------------------------
			unsigned short int basSize;

			stat (fileNamePointer[fileIdx], &stat_p);
			basSize = stat_p.st_size;
			printf ("\nBASIC file, %d bytes\n", basSize);

			fread (&binBuf, basSize-1, 1, infile);

			start = 0x8001;
			end = start + basSize - 1;
			printf ("\nFile: %s; Start: %x, End: %x", fileNamePointer[fileIdx], start, end);
			Send ();

			Poke (0xF6C2, end % 0x100);
			waitRx();

			Poke (0xF6C3, end / 0x100);
			waitRx();

			printf ("\nSending BASIC file done.");

		}
		else if (ch == 0x41)
		{
			/************************************************************************
			 * ROM images
			 ************************************************************************/
			int numblocks, i;

			stat (fileNamePointer[fileIdx], &stat_p);
			printf ("\nROM file, %llu bytes\n", (unsigned long long) stat_p.st_size);


			romSize = stat_p.st_size;
			if (romSize > SIZE32K)
			{
				printf ("\nSending ROMs bigger than 32K is not supported\n");
				exit(1);
			}
			if (romSize != 8*1024 && romSize != SIZE16K && romSize != SIZE32K)
			{
				printf ("\nSending ROMs with non-standard size (not 8, 16 or 32K) is not supported\n");
				exit(1);
			}

			if (romSize < SIZE32K)
			{
				start = 0x9000;
				end = start + romSize + sizeof(ROM2BIN_startCode) - 1;
				run = start + romSize;

				rewind (infile);
				fread (&binBuf, romSize, 1, infile);
				memcpy(&binBuf[romSize], ROM2BIN_startCode, sizeof (ROM2BIN_startCode));

				binBuf[0] = 0; // destroy "AB" signature so it won't reboot

				printf ("Start: %x, End: %x, Run: %x\n", start, end, run);

				Send ();
				Run ();
			}
			else
			{
				// send 1st 16K part

				start = 0x9000;
				end = start + SIZE16K + sizeof(ROM2BIN_startCode) - 1;
				run = start + SIZE16K;

				rewind (infile);
				fread (&binBuf, SIZE16K, 1, infile);
				memcpy(&binBuf[SIZE16K], ROM2BIN_startCode, sizeof (ROM2BIN_startCode));
				// replace last Z80 command "jp hl" with "nop"
				binBuf[SIZE16K + sizeof(ROM2BIN_startCode) - 9] = 0x00;
				// destroy "AB" signature so it won't restart ROM on reboot
				binBuf[0] = 0;

				printf ("\nSending 1st 16K ROM part: Start: %x, End: %x, Run: %x\n", start, end, run);

				Send ();
				Run ();

				// send 2nd 16K part

				fread (&binBuf, SIZE16K, 1, infile);
				memcpy(&binBuf[SIZE16K], ROM2BIN_startCode, sizeof (ROM2BIN_startCode));
				// replace Z80 command "ld de,4000" with "ld de,8000"
				binBuf[SIZE16K + sizeof(ROM2BIN_startCode) - 21] = 0x80;

				printf ("\n\nSending 2nd 16K ROM part: Start: %x, End: %x, Run: %x\n", start, end, run);

				usleep (500000); // pause after previous 16K part
				Send ();
				Run ();
			}

			printf ("\nSending ROM done.");
		}
		else
		{
			printf("\nUnsupported file type - can't send.\n");
			exit(1);
		}
	}
   return 0;
}

// ------------------------------------------------------------------------------------
void Send ()
{

		int binBufOffset = 0, i, waitRes;

		// *************************
		pingPacket[2] = studentNo;
		for (i=0; i < 3; i++)
		{
			write (fd, pingPacket, sizeof (pingPacket));
			if (verbose) printf("\nSent ping No.%d", i+1);
			waitRes = waitRx();
			if (waitRes) break;
		}
		if (! waitRes)
		{
			printf("\nFailed: no reply from PING\n");
			exit(1);
		}

        checksum [0] = checksum [1] = 0;
        SendHeader ();
		if (verbose) printf("\nSent header");

		waitRes = waitRx();
		if (! waitRes)
		{
			printf("\nFailed: no reply from header packet\n");
			exit(1);
		}

        idx = (end - start) / MAXBLKSIZE;
        printf ("\n%d blocks to send: ", idx);
        current = start;
        for (i = 0; i < idx; i++)
        {
			if (verbose)
			{
				printf("\nSent block No.%d", i+1);
			}
			else
			{
				if ((i+1) % 10 == 0)
					printf (". %d ", i+1);
				else
					printf (".");

				fflush(stdout);
			}

    		memcpy(buf, &binBuf[binBufOffset], sizeof (buf));
    		binBufOffset += sizeof (buf);

            SendBuf (MAXBLKSIZE);
            current += MAXBLKSIZE;

			waitRes = waitRx();
			if (! waitRes)
			{
				printf("\nFailed: no reply from SendBuf packet\n");
				exit(1);
			}
		}

        // Calculate the rest
        if (verbose) printf ("\nLast block: %d bytes\n", end - current + 1 );
		memcpy(buf, &binBuf[binBufOffset], end - current + 1);
        SendBuf (end - current + 1);

		waitRes = waitRx();
		if (! waitRes)
		{
			printf("\nFailed: no reply from SendBuf packet\n");
			exit(1);
		}
}


// ------------------------------------------------------------------------------------
void Run ()
{
	unsigned char hdr [] = {0xf0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x48,
			0x00, 0x00, 0x00, 0x00};
	unsigned char string [40];
	unsigned char ch;
	unsigned int i;

/* defusr=&hXXXX:?usr(0)
   123456789012345678901234567890123
   _nete:defusr=&h0000:?usr(0):_neti
*/


    hdr [SRC] = 0;
    hdr [DST] = studentNo;
    hdr [7] = 0;
    hdr [8] = 0;
    hdr [9] = 0;
    hdr [10] = 34;
    write (fd, hdr, sizeof (hdr));

	if (verbose)
	{
		printf ("\nHeader: ");
		for (i = 0; i < sizeof (hdr); i++)
		{
			printf ("%.2x ", hdr [i]);
		}
	}

    sprintf (string, " _nete:DefUsr=&H%.4x:?Usr(0):_neti", run);

    printf ("\nRun command: '%s'", string);

    checksum [0] = checksum [1] = 0;
    for (i = 0; i < 34; i++)
    {
		SendChar (string [i]);
		UpdateChecksum (string [i]);
    }
    SendChecksum ();
    ch = 0x83;
    write (fd, &ch, 1);
}

// ------------------------------------------------------------------------------------
void SendBuf (unsigned short int len)
{
	unsigned char hdr [] = {0xf0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x42,
							0x00, 0x00, 0x00, 0x00};
	unsigned char hi, lo;
	unsigned int q;

    hdr [SRC] = 0;
    hdr [DST] = studentNo;

    hi = (len >> 8) & 0xff;
    lo = len & 0xff;

    hdr [7] = (hi >> 7) & 0x01;
    hdr [8] = hi & 0x7f;
    hdr [9] = (lo >> 7) & 0x01;
    hdr [10] = lo & 0x7f;

    if (verbose)
    {
		printf ("\nHeader: ");
		for (q = 0; q < sizeof (hdr); q++)
			printf ("%.2x ", hdr [q]);

		printf ("\nData: ");
	}

    write (fd, hdr, sizeof (hdr));

    checksum [0] = checksum [1] = 0;

    for (q = 0; q < len; q++)
    {
		SendChar (buf [q]);
		UpdateChecksum (buf [q]);
    }

    SendChecksum ();

    if (len >= MAXBLKSIZE)
		lo = 0x97;
    else
		lo = 0x83;

    write (fd, &lo, 1);

    fflush(0);
}

// ------------------------------------------------------------------------------------
void SendChar (unsigned char ch)
{

int i;

unsigned char c = (ch & 0x80) >> 7;

    if (verbose) printf ("%.2x ", c);

    write (fd, &c, 1);
    c = ch & 0x7f;
    write (fd, &c, 1);

    if (verbose) printf ("%.2x ", c);
}

// ------------------------------------------------------------------------------------
void UpdateChecksum (unsigned char ch)
{

    // printf ("\nOld CRC16: %2x%2x, ch:%2x", checksum [0], checksum [1], ch);

    if ((checksum [1] + ch) > 0xff)
    {
		checksum [0] ++;
    }

    checksum [1] += ch & 0xff;

    // printf ("\nCRC16: %2x%2x", checksum [0], checksum [1]);
}

// ------------------------------------------------------------------------------------
void SendChecksum ()
{
	unsigned char crc16 [4];
	unsigned char hi, lo;


    crc16 [0] = (checksum [0] >> 7) & 0x01;
    crc16 [1] = checksum [0] & 0x7f;
    crc16 [2] = (checksum [1] >> 7) & 0x01;
    crc16 [3] = checksum [1] & 0x7f;

    write (fd, crc16, sizeof (crc16));

    if (verbose) printf ("\nCRC16: %.2x%.2x", checksum [0], checksum [1]);
}

// ------------------------------------------------------------------------------------
void SendHeader ()
{
	unsigned char hdr [] = {0xf0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x52,
				0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x83};
	unsigned char hi, lo;

    hdr [SRC] = 0;
    hdr [DST] = studentNo;

    hi = (start >> 8) & 0xff;
    lo = start & 0xff;

    hdr [7] = (hi >> 7) & 0x01;
    hdr [8] = hi & 0x7f;
    hdr [9] = (lo >> 7) & 0x01;
    hdr [10] = lo & 0x7f;

    hi = (end >> 8) & 0xff;
    lo = end & 0xff;

    hdr [11] = (hi >> 7) & 0x01;
    hdr [12] = hi & 0x7f;
    hdr [13] = (lo >> 7) & 0x01;
    hdr [14] = lo & 0x7f;

    write (fd, hdr, sizeof (hdr));

    if (verbose)
    {
		printf ("\nHeader: ");

    	for (i = 0; i < sizeof (hdr); i++)
    	{
			printf ("%.2x ", hdr [i]);
		}
	}

}

// ------------------------------------------------------------------------------------
void Poke (unsigned int addr, unsigned char byte)
{
unsigned char poke [] = {0xf0, 0x00, 0x00, 0x01, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83};
//			 0     1     2     3     4     5     6     7     8     9     a     b     c     d
unsigned char hi, lo;
int i;

    poke [SRC] = 0;
    poke [DST] = studentNo;

    hi = (addr >> 8) & 0xff;
    lo = addr & 0xff;

    poke [7] = (hi >> 7) & 0x01;
    poke [8] = hi & 0x7f;
    poke [9] = (lo >> 7) & 0x01;
    poke [10] = lo & 0x7f;

    poke [11] = (byte >> 7) & 0x01;
    poke [12] = byte & 0x7f;

    write (fd, poke, sizeof (poke));

    if (verbose)
    {
		printf ("\nPoke %.4x, %.2x : ", addr, byte);
	    for (i = 0; i < sizeof (poke); i++)
		{
			printf ("%.2x ", poke [i]);
		}
	}

}

// ------------------------------------------------------------------------------------
// signal handler. sets rxFlag to 1, to indicate above loop that characters have been received.                                           *
// ------------------------------------------------------------------------------------
void signal_handler_IO (int status)
{
int i, n;

	n = read (fd, &(buf [pos]), 1024 - pos);
	pos += n;

	// Debug

	if (verbose)
	{
		printf ("\n****************************************");
		printf ("\nRecvData: got %d bytes, pos=%d, last byte: 0x%.2x\n", n, pos, buf [pos]);
		for (i = 0; i < pos; i++)
		{
			printf ("%.2x ", buf [i]);
		}
		printf ("\n****************************************\n");
	}

	if (pos > 256)
	{
		pos = 0;
	}
	else
	{
		if ( (buf [pos - 1] == 0x83) || (buf [pos - 1] == 0x97))
		{
			rxFlag = 1;
		}
	}

}

// ------------------------------------------------------------------------------------
int waitRx()
{
	int readfdSet, count = 0, delay = 200000;

// for some strange reason I saw usleep on Cygwin is 3 times longer than on Linux on the same machine
#ifdef __CYGWIN__
	delay = 60000;
#endif
	signal(SIGIO, SIG_IGN); // the important one.
	rxFlag = pos =  0;

	if (studentNo == 127)
	{
	 	usleep(delay);
		return 1;
	}

	while(1)
	{
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;

		FD_ZERO(&fdSet);
		FD_SET(fd, &fdSet);

		readfdSet = select(fd + 1,
						&fdSet,
						(fd_set *) 0,
						(fd_set *) 0,
						&timeout);

		count++;

		if (readfdSet < 0)
		{
			printf("\nError in select()\n");
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

// --------------------------------------------------------------------------
// Sections of code used in debugging, but not used anymore
// --------------------------------------------------------------------------


/*
unsigned char checksumProgramCode[] =
{
	0xF3,				// di
	0xE5,				// push hl
	0x11,0x11,0xA0,		// ld de,A000
	0x01,0x00,0x40,		// ld bc,4000
	0x26,0x00,			// h,00
	0x6C,				// ld l,h
	0xC5,				// LOOP: push bc
	0x1A,				// ld a,(de)
	0x4F,				// ld c,a
	0x06,0x00,			// ld b,00
	0x13,				// inc de
	0x09,				// add hl,bc
	0xC1,				// pop bc
	0x0B,				// dec bc
	0x78,				// ld a,b
	0xB1,				// or c
	0x20,0xF3,			// jr nz, LOOP
	0xDD,0xE1,			// pop ix
	0xDD,0x75,0x02,		// ld (ix+02),l
	0xDD,0x74,0x03,		// ld (ix+03),h
	0x3E,0x02,			// ld a,02
	0xC9				// ret
};
*/

	/*
			// send checksum checker and compare
			checksum = 0;
			for (i = 0; i < (end-start+1); i++)
			{
				checksum += binBuf[i];
			}
			printf("\nTotal Checksum: %d", checksum);

			memcpy(binBuf, checksumProgramCode, sizeof (checksumProgramCode));
			binBuf[3] = start % 0x100;
			binBuf[4] = start / 0x100;

			binBuf[6] = (end-start+1) % 0x100;
			binBuf[7] = (end-start+1) / 0x100;

			start = run = 0x8100; // 0xF000
			end = start + sizeof (checksumProgramCode) - 1;
			printf ("\nFile: %s; Start: %x, End: %x, Run: %x", fileNamePointer[fileIdx], start, end, run);

			Send ();
			Run ();
			usleep (1000000);
	*/
