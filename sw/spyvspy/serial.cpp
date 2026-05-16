#include <inttypes.h>
#include <fcntl.h>
//#include <termios.h>
#include <string.h>
//#include <unistd.h>
#include <stdlib.h>
//#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include "commands.h"

#include "serial.h"
#include "diags.h"

SerialPort::SerialPort(const char* device, int stNo)
{
	studentNo = stNo;
	char portName[12] = "\\\\.\\COM";
	sprintf_s(portName, "%s%s", portName, device);

	handler = CreateFileA(portName,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);
	//m_fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
}

int SerialPort::Setup()
{
	DCB dcbSerialParameters = { 0 };

	if (!GetCommState(handler, &dcbSerialParameters))
	{
		puts("MsxSerialPort create ERROR: failed to get current serial port parameters");
	}
	else
	{
		dcbSerialParameters.BaudRate = MSX_BAUDRATE;
		dcbSerialParameters.ByteSize = MSX_RYTESIZE;
		dcbSerialParameters.StopBits = MSX_STOPBITS;	//ONESTOPBIT;
		dcbSerialParameters.Parity = MSX_PARITY;	//NOPARITY;
		dcbSerialParameters.fDtrControl = DTR_CONTROL_DISABLE/*DTR_CONTROL_ENABLE*/;

		if (!SetCommState(handler, &dcbSerialParameters))
		{
			puts("MsxSerialPort create WARNING: could not set serial port parameters");
		}
		else
		{
			//connected = true;
			PurgeComm(handler, PURGE_RXCLEAR | PURGE_TXCLEAR);
			Sleep(MSX_WAIT_TIME);
			return ERR_NONE;
		}
	}
	CloseHandle(handler);

	return ERR_WTF;
}

#define MAXPKTSIZE	128

bool SerialPort::checkPacket(unsigned char* buf, int size)
{
	int src, dst;
	int valid = 0;

	if (size < 5)
	{
		if (verbose) { if (size > 1) printf("Wrong packet read[%d]: ", size);
		dump(buf, size); puts(""); }
		return false;
	}
	// Header
	if ((buf[0] == 0xf0 || buf[0] == 0x78 || buf[0] == 0x70) && (buf[1] == 00))	//accepted or not accepted ...?
	{
		//if(buf[0] != 0xf0 && buf[3] != PONG) {puts("???: ");msx.dump(buf,size);}
		dst = buf[2];
		src = buf[4];
		//if ((teacher ? src != studentNo : dst != studentNo))
		//	return false;
		bool verb = verbose && buf[3] != PING;
		if (verb || verbose > 2) dump(buf, size, "read[%d]: ", true);
		unsigned int word;
		const char* msg = NULL;
		switch (buf[3])
		{
		case BASE:
			switch (buf[6])
			{
			case RE_NET_CREATE_FILE:
				msg = "RE_NET_CREATE_FILE";
			case RE_NET_CLOSE_FILE:
				msg = msg ? msg : "RE_NET_CLOSE_FILE";
			case RE_NET_WRITE_FILE:
				msg = msg ? msg : "RE_NET_WRITE_FILE";
				if (verbose) printf("*** %s from %d to %d\n", msg, src, dst);
				word = getWord(&(buf[7]));
				if (verbose) printf("*** Payload: %d bytes\n", word);
				getRxData(buf);
				break;
			case SEND_FILE:
				if (verbose) printf("*** receive SEND_FILE\n");
				Ack();
				break;
			default:
				if (verbose) printf("*** Unknown BASE packet 0x%.2x ***\n", buf[6]);
				Ack();
				break;
			}
			break;
		case PING:
			msg = msg ? msg : "PING";
			Pong();
		case PONG:
			msg = msg ? msg : "PONG";
		case ACK:
			msg = msg ? msg : "ACK";
			if (verb) printf("*** %s from %d to %d\n", (msg ? msg : "#Unknown#"), src, dst);
			break;
		}
	}
	return true;
}

void SerialPort::getRxData(unsigned char* p)
{
	RxData.H = getByte(&p[11]);
	RxData.F = getByte(&p[13]);
	RxData.A = getByte(&p[15]);
	if (verbose) printf("\n *** H: %.2x F: %.2x A: %.2x FCB: ", RxData.H, RxData.F, RxData.A);
	for (int i = 0; i < sizeof(RxData.FCB); i++)
	{
		RxData.FCB[i] = getByte(&p[17 + i * 2]);
		if (verbose) printf(" %.2x", RxData.FCB[i]);
	}
	if (verbose) puts("");
}

bool SerialPort::Pong(int dstAddr /*= 0*/, int srcAddr /*= 0*/)
{
	//								0x78
	unsigned char packet[] = { 0xF0, 0x00, 0x00, 0x15, 0x00, 0x83 };
	srcAddr = srcAddr ? srcAddr : studentNo;
	packet[2] = !teacher ? dstAddr : srcAddr;
	packet[4] = !teacher ? srcAddr : dstAddr;

	if (verbose > 1) printf("Repl pong to %d\n", dstAddr);
	if (!write(packet, sizeof(packet)))
	{
		puts("Pong write error\n");
		return false;
	}
	return true;
}

bool SerialPort::Ack(int dstAddr /*= 0*/, int srcAddr /*= 0*/)
{
	//								0x78
	unsigned char packet[] = { 0xF0, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x83 };
	srcAddr = srcAddr ? srcAddr : studentNo;
	packet[2] = !teacher ? dstAddr : srcAddr;
	packet[4] = !teacher ? srcAddr : dstAddr;

	if (verbose) printf("Repl ack to %d\n", dstAddr);
	if (!write(packet, sizeof(packet)))
	{
		puts("Ack write error\n");
		return false;
	}
	return true;
}

int SerialPort::waitRx(const int intents /*= 10*/)
{
	unsigned char* rbuf = NULL;
	int  delay = 200;
	unsigned char lbuf[MAXPKTSIZE], * buf = rbuf ? rbuf : lbuf;

	if (verbose > 2) printf("WaitRx read: ");
	for (int i = 0; i < MAXPKTSIZE;)
	{
		int n = 0, err = 0;
		//			for(int j = 5; j-- && !(n = msx.read((char *)&buf[i], sizeof(buf)-i)); Sleep( delay ));
		for (int j = 5; j-- && !(n = read((uint8_t*)&buf[i], 1/*, verbose > 2*/)); /*Sleep( delay )*/);
		if (!n && (err = GetLastError()))
		{
			printf("WaitRx error %d (%d bytes read)\n", GetLastError(), i);
			dump(buf, i + 1, NULL, true);
			//ERRno = Read_ERR | WaitRx_ERR;
			return 0;
		}
		i += n;
		if ((buf[i - 1] == LAST) || (buf[i - 1] == INTERMEDIATE))
		{
			//				if( verbose > 1 ) msx.dump( buf, i, "WaitRx[%d]: ", true );
			checkPacket(buf, i);
			if (verbose > 2) puts("\nWaitRx ok");
			return true;
		}
	}
	//ERRno = WaitRx_ERR;
	puts("\nWaitRx fail");
	return false;
}

void SerialPort::dump(const unsigned char* buf, int size, const char* msg /*= NULL*/, bool end /*= false*/) const
{
	if (msg) printf(msg, size);
	for (int i = 0; i < size; printf("%02X ", buf[i++]));
	if (end) puts("");
}

int SerialPort::read(uint8_t* buf, size_t len)
{
	DWORD bytesRead;
	size_t toRead = 0;
	size_t size = len;
	char *buffer = (char*) buf;
	ClearCommError(handler, &errors, &status);

	if (status.cbInQue > 0)
	{
		toRead = status.cbInQue > size ? size : status.cbInQue;
		memset(buffer, 0, size);

		if (ReadFile(handler, buffer, (DWORD)toRead, &bytesRead, NULL) && bytesRead)
		{
			if (verbose) { if (bytesRead > 1) printf("read[%d]: ", bytesRead); dump((const unsigned char*)buffer, bytesRead); }
			return bytesRead;
		}
	}

	return 0;
}

size_t SerialPort::write(uint8_t* buf, size_t len) const
{
	DWORD bytesSend;
	size_t size = len;
	char* buffer = (char*)buf;
	if (verbose)
	{
		if (size > 1)
			printf("to write[%d]: ", (int)size);
		dump((const unsigned char*)buffer, (int)size);
	}
	if (!WriteFile(handler, (void*)buffer, (DWORD)size, &bytesSend, 0))
	{
		ClearCommError(handler, &errors, &status);
		puts("MsxSerialPort::write error");
		return false;
	}
	else
	{
		if (FlushFileBuffers(handler))
		{
			if (verbose) puts("write ok");
		}
		else
			puts("write flush error");
		return true;
	}
}

void usleep(int us)
{
	Sleep(us / 1000);
}

void sleep(int s)
{
	Sleep(s * 1000);
}
