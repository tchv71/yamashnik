#pragma once

#include <inttypes.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
//#include <unistd.h>
//#include <termios.h>
#include "util.h"

#include "diags.h"

#define BAUDRATE 		38400
#define SELECT_WAIT		10000
#define MSX_WAIT_TIME	1000
#define MAX_DATA_LENGTH	255
#define MSX_BAUDRATE	CBR_38400
#define MSX_RYTESIZE	8
#define MSX_STOPBITS	2
#define MSX_PARITY		EVENPARITY

enum _SIO_ERR { ERR_NONE, ERR_NOFILE, ERR_WTF };

class SerialPort;

typedef struct _xdata {
	uint8_t H;
	uint8_t F;
	uint8_t A;
	uint8_t FCB[37];
	_xdata() {}

	_xdata(_xdata& origin) : H(origin.H), F(origin.F), A(origin.A)
	{
		memcpy(&FCB, &origin.FCB, sizeof(FCB));
	}

	_xdata(uint8_t _H, uint8_t _F, uint8_t _A, const char* fileName) : H(_H), F(_F), A(_A)
	{
		memset(&FCB[0], 0, sizeof(FCB));
		FCB[0] = 8;
		Util::dosname(fileName, (char*)&FCB[1]);
	}
} NetFCB;


class SerialListener {
public:
	virtual int RxHandler() = 0;
};

class SerialPort
{
private:
	HANDLE handler;
	mutable COMSTAT status;
	mutable DWORD errors;

	SerialListener* m_RxListener;
	int verbose = 0;
public:
	struct xData : public NetFCB
	{
		xData()
		{
			init();
		}
		void init()
		{
			memset(this, 0, sizeof(xData));
		}
		void setFileName(const char* name)
		{
			FCB[0] = 8;
			int i = 0;
			// copy filename
			for (; i < 8; i++)
			{
				if (name[i] == '.')
					break;
				FCB[i + 1] = toupper(name[i]);
			}
			for (; i < 11; FCB[++i] = ' ');
			// and extension
			size_t n = strlen(name);
			for (i = 0; i < 4; i++)
				FCB[12 - i] = toupper(name[n - i]);
		}
	} /*TxData,*/ m_RxData;
	int teacher = 0;
	int studentNo = 127;
	SerialPort(const char* device, int stNo);
	~SerialPort()
	{
		if (handler != nullptr) CloseHandle(handler);
		handler = nullptr;
	}

	int Setup();

	void SendByte(unsigned char b) const
	{
		WriteFile(handler, (void*)&b, 1, nullptr, 0);
	}

	int read(uint8_t* buf, size_t len);
	size_t write(uint8_t* buf, size_t len) const;

	void SetRxListener(SerialListener* listener)
	{
		m_RxListener = listener;
	};


	void dump(const unsigned char* buf, int size, const char* msg = NULL, bool end = false) const;
	bool checkPacket(unsigned char* buf, int size);
	unsigned char getByte(unsigned char* p)
	{
		return (p[1] + (p[0] ? 128 : 0));
	}
	unsigned int getWord(unsigned char* p)
	{
		return ((getByte(p) << 8) + getByte(p + 2));
	}
	void getRxData(unsigned char* p);
	bool Pong(int dstAddr = 0, int srcAddr = 0);
	bool Ack(int dstAddr = 0, int srcAddr = 0);
	int waitRx(const int intents = 10);

};

void usleep(int us);

void sleep(int s);