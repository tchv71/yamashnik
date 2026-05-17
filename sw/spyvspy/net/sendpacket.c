#ifndef __SENDPACKET__
#define __SENDPACKET__

#include <stdio.h>
#include "commands.h"

extern int rxFlag;
extern int pos;
extern int verbose;

void SendByte (int fd, unsigned char b) {
	// printf ("%.2x ", b);
        write (fd, &b, 1);
}

void SendEscapedByte (int fd, unsigned char b) {
        SendByte (fd, (b & 128) >> 7);
        SendByte (fd, b & 127);
}

void SendEscapedWord (int fd, unsigned int w) {
	SendEscapedByte (fd, (w >> 8));
	SendEscapedByte (fd, w & 255);
}

void SendHeader (int fd, unsigned char *h) {
int i;
	for (i = 0; i < 5; i++)
		SendByte (fd, h [i]);
}

void SendEscapedBlockWithChecksum (int fd, unsigned char *buf, int len) {
unsigned int checksum = 0;
int i;
	for (i = 0; i < len; i++) {
		SendEscapedByte (fd, buf [i]);
		checksum += buf [i];
	}
	SendEscapedWord (fd, checksum);
}

unsigned char ReadEscapedByte (unsigned char *p) {
        return (p [1] + (p [0] ? 128 : 0));
}

unsigned int ReadEscapedWord (unsigned char *p) {
	return ((ReadEscapedByte (p) << 8) + ReadEscapedByte (p + 2));
}


/*
 *
 * SendPacket
 *
 *
 */
void SendPacket (int fd, int srcAddr, int dstAddr, int cmdType, unsigned char *buf, int len) {
unsigned char Header [] = { 0xf0, 0x00, 0x00, 0x01, 0x00 };
int i, l;

  if (verbose) printf ("\nSendPacket: from %d to %d, cmd: 0x%.2x, %d bytes",
	srcAddr, dstAddr, cmdType, len);

  // Header fields, common for all packets
  Header [2] = dstAddr;
  Header [4] = srcAddr;

  switch (cmdType) {
	case PING:
		Header [3] = PING;
		SendHeader (fd, Header);
		SendByte (fd, LAST);				// TERMINATOR
		break;

	case NET_CREATE_FILE:
		SendHeader (fd, Header);			// HEADER
		SendEscapedByte (fd, NET_CREATE_FILE);		// COMMAND
		SendEscapedWord (fd, len);			// LENGTH: FCB (37 bytes) + H, F, A (3 bytes)
		SendEscapedBlockWithChecksum (fd, buf, len);	// PAYLOAD + CHECHKSUM
		SendByte (fd, LAST);				// TERMINATOR
		break;

	case NET_CLOSE_FILE:
                SendHeader (fd, Header);                        // HEADER
                SendEscapedByte (fd, NET_CLOSE_FILE);           // COMMAND
                SendEscapedWord (fd, len);                      // LENGTH: FCB (37 bytes) + H, F, A (3 bytes)
                SendEscapedBlockWithChecksum (fd, buf, len);    // PAYLOAD + CHECHKSUM
                SendByte (fd, LAST);                            // TERMINATOR
                break;

	case NET_MASTER_DATA:
		l = len;
		do {
			rxFlag = pos = 0;

			SendHeader (fd, Header);
			SendEscapedByte (fd, (l == len) ? NET_MASTER_DATA : NET_MASTER_DATA2);

			SendEscapedWord (fd, (l > 56) ? 56 : l);
			SendEscapedBlockWithChecksum (fd, buf + len - l, (l > 56) ? 56 : l);
			SendByte (fd, (l > 56) ? INTERMEDIATE : LAST);

			l -= 56;

			waitRx();


		} while (l > 0);

		break;

	case NET_WRITE_FILE:
		SendHeader (fd, Header);
                SendEscapedByte (fd, NET_WRITE_FILE);
                SendEscapedWord (fd, len);
                SendEscapedBlockWithChecksum (fd, buf, len);
                SendByte (fd, LAST);
		break;

  }


  // Debug
  if (verbose) printf ("\n");

}

/*
 *
 * SendBlock	- send data block. if the block is bigger than MTU, send a chain of smaller packets
 *		paying attention to packet's cmdType and it's terminator
 *
 */
void SendBlock (int fd, int srcAddr, int dstAddr, int cmdType, unsigned char *buf, int len) {
}


#endif
