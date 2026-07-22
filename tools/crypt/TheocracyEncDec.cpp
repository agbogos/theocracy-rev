// TheocracyEncDec.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

void XorBuff(char* buff, int size)
{
	char key1[] = "theocracy sux";
	char key2[] = "mutant technology";

	for(int i = 0; i < size; i++)
	{
		buff[i] = buff[i] ^ key2[i%0x11] ^ key1[i%0x0D];
	}
}

void SaveDec(char* filename, char* buff, int size)
{
	FILE* hFile = fopen(filename, "wb");
	fwrite(buff, 1, size, hFile);
	fclose(hFile);
}

void SaveEnc(char* filename, char* buff, int size)
{
	FILE* hFile = fopen(filename, "wb");
	fwrite("RSA4096", 1, 7, hFile);
	fwrite(buff, 1, size, hFile);
	fclose(hFile);
}

int _tmain(int argc, _TCHAR* argv[])
{
	FILE* hFile = fopen(argv[1], "rb");

	fseek(hFile, 0, SEEK_END);
	int size = ftell(hFile)-7;
	fseek(hFile, 0, SEEK_SET);

	if(size > 0)
	{
		char Header[7] = {0};
		fread(Header, 1, 7, hFile);

		if(!strncmp(Header, "RSA4096", 7))
		{
			char* buff = new char[size];
			fread(buff, 1, size, hFile);
			fclose(hFile);

			XorBuff(buff, size);

			SaveDec(argv[1], buff, size);
		}
		else{
			fseek(hFile, 0, SEEK_SET);
			size+=7;

			char* buff = new char[size];
			fread(buff, 1, size, hFile);
			fclose(hFile);

			XorBuff(buff, size);

			SaveEnc(argv[1], buff, size);
		}
	}
	else{
		size+=7;

		char* buff = new char[size];
		fread(buff, 1, size, hFile);
		fclose(hFile);

		XorBuff(buff, size);

		SaveEnc(argv[1], buff, size);
	}

	return 0;
}

