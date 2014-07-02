//
// Copyright (C) 2012 - Cabin Programs, Ken Keller 
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>

#ifndef FALSE
#define FALSE (0)
#define TRUE (!(FALSE))
#endif

#define sizeEEPROM 244
unsigned char eeprom[sizeEEPROM+100];	// Give a little extra for no good reason




#define NB_PIN 27



int logibone_mux_tab [NB_PIN][3] = {
        {8, 25,0xE030},
	{8, 24,0xE030},
	{8, 5,0xE030},
	{8, 6,0xE030},
	{8, 23,0xE030},
	{8, 22,0xE030},
	{8, 3,0xE030},
	{8, 4,0xE030},
	{8, 19,0xE030},
        {8, 13,0xE030},
	{8, 14,0xE030},
	{8, 17,0xE030},
        {8, 12,0xE030},
	{8, 11,0xE030},
	{8, 16,0xE030},
	{8, 15,0xE030},

	{8, 18,0xC028},
	
	{8, 21,0xC008},
	{8, 7,0xC008},
	{8, 8,0xC008},
	{8, 10,0xC008},
	{8, 9,0xC008},
	{9, 12,0xC008},

	{9, 28,0xC013},
	{9, 29,0xC033},
	{9, 30,0xC013},
        {9, 31,0xC013}
};




int eepromIndex[2][46] = {
	{ -1, -1, 140, 142, 132, 134, 170, 176, 172, 174, 146, 144, 118, 120,
		150, 148, 122, 168, 116, 166, 164, 138, 136, 130, 128, 162, 202, 206,
		204, 208, 102, 104, 100, 200, 98, 198, 194, 196, 190, 192, 186, 188,
		182, 184, 178, 180 },
	{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 124, 160, 126, 156, 152, 158,
		94, 92, 106, 108, 90, 88, 154, 112, 220, 110, 216, 214, 210, 212, 218,
		-1, 230, -1, 234, 232, 226, 228, 222, 224, 114, 96, -1, -1, -1, -1 }
};

int main(int argc, char* argv[])
{
	int number1, number2, numberPin;
	char keypressed;
	char buffer[100];
	int index, i;

	printf("\n\n---EEPROM MAKER---\n\nThis is a program to make the EEPROM data file for a BeagleBone Cape.\n");
	printf("\nThis program produces an output file named: data.eeprom\n");
	printf("The data file follows EEPROM Format Revision 'A0'\n");
	printf("This data file can be put in the BeagleBone EEPROM by this command on a BeagleBone:\n");
	printf("   > cat data.eeprom >/sys/bus/i2c/drivers/at24/3-005x/eeprom\n");
	printf("         Where:  5x is 54, 55, 56, 57 depending on Cape addressing.\n");
	printf("         NOTE:  See blog.azkeller.com for more details.\n");
	printf("\n+++ No warranties or support is implied - sorry for CYA +++\n\n");

	for(index=0; index<sizeEEPROM; index++)
		eeprom[index]=0x00;

	eeprom[0] = 0xaa;
	eeprom[1] = 0x55;
	eeprom[2] = 0x33;
	eeprom[3] = 0xee;
	eeprom[4] = 0x41;
	eeprom[5] = 0x30;

	printf("Enter Name of Board in ASCII (max 32): ");
	gets(buffer);
	number2 = strlen(buffer);
	if (number2>32) number2=32;
	for(index=0; index<number2; index++)
		eeprom[6+index]=buffer[index];

	printf("Enter HW Version of Board in ASCII (max 4): ");
	gets(buffer);
	number2 = strlen(buffer);
	if (number2>4) number2=4;
	for(index=0; index<number2; index++)
		eeprom[38+index]=buffer[index];

	printf("Enter Name of Manufacturer in ASCII (max 16): ");
	gets(buffer);
	number2 = strlen(buffer);
	if (number2>16) number2=16;
	for(index=0; index<number2; index++)
		eeprom[42+index]=buffer[index];

	printf("Enter Part Number in ASCII (max 16): ");
	gets(buffer);
	number2 = strlen(buffer);
	if (number2>16) number2=16;
	for(index=0; index<number2; index++)
		eeprom[58+index]=buffer[index];

	printf("Enter Serial Number in ASCII (max 16): ");
	gets(buffer);
	number2 = strlen(buffer);
	if (number2>16) number2=16;
	for(index=0; index<number2; index++)
		eeprom[76+index]=buffer[index];

	do {
		printf("Enter MAX Current (mA) on VDD_3V3EXP Used by Cape (Range 0 to 250mA): ");
		scanf(" %d",&number1);getchar();
	} while (number1 > 250);
	eeprom[236]=number1>>8;
	eeprom[237]=number1 & 0xff;

	do {
		printf("Enter MAX Current (mA) on VDD_5V Used by Cape (Range 0 to 1000mA): ");
		scanf(" %d",&number1);getchar();
	} while (number1 > 1000);
	eeprom[238]=number1>>8;
	eeprom[239]=number1 & 0xff;

	do {
		printf("Enter MAX Current (mA) on SYS_5V Used by Cape (Range 0 to 250mA): ");
		scanf(" %d",&number1);getchar();
	} while (number1 > 250);
	eeprom[240]=number1>>8;
	eeprom[241]=number1 & 0xff;

	do {
		printf("Enter Current (mA) Supplied on VDD_5V by Cape (Range 0 to 65535mA): ");
		scanf(" %d",&number1);getchar();
	} while (number1 > 65535);
	eeprom[242]=number1>>8;
	eeprom[243]=number1 & 0xff;


	eeprom[75]=NB_PIN;
	for(i= 0; i < NB_PIN ; i ++){
		unsigned char upper, lower, pin, connector ;
		connector = logibone_mux_tab[i][0];
		pin = logibone_mux_tab[i][1] ;
		upper = (logibone_mux_tab[i][2] & 0xFF00) >> 8;
		lower = (logibone_mux_tab[i][2] & 0x00FF) ;
		eeprom[eepromIndex[connector-8][pin-1]] = upper;
		eeprom[eepromIndex[connector-8][pin-1]+1] = lower;	
	}
	
	/*
	do {
		printf("\nEnter Number of Pins Used by Cape (Range 0 to 74): ");
		scanf(" %d",&numberPin);getchar();
	} while (numberPin > 74);
	eeprom[75]=numberPin;

	int connector, pin, usage, type, slew, rx, pull, pullEnabled, mux, validFlag;

	for(index=0; index<numberPin; index++)
	{
		do {
			printf("\nGet data for pin %d\n",index+1);

			validFlag = TRUE;
			do {
				printf("\tPIN # %d - Enter Connector number (8 or 9): ",index+1);
				scanf(" %d",&connector);getchar();
			} while (connector < 8 || connector > 9);
	
			do {
				printf("\tPIN # %d - Enter pin number (1 through 46): ",index+1);
				scanf(" %d",&pin);getchar();
			} while (pin < 1 || pin > 46);

			if (eepromIndex[connector-8][pin-1] == -1)
			{
				validFlag = FALSE;
				printf("\n*** P%d_%d Can't be used by a Cape... ***\n",connector,pin);
			}
		} while (!validFlag);

		do {
			printf("\tPIN # %d P%d_%d - Usage? 1=pin used, 0=unused: ",index+1,connector,pin);
			scanf(" %d",&usage);getchar();
		} while (usage < 0 || usage > 1);

		do {
			printf("\tPIN # %d P%d_%d - Type? 1=input, 2=output, 3=bidirectional: ",index+1,connector,pin);
			scanf(" %d",&type);getchar();
		} while (type < 1 || type > 3);

		do {
			printf("\tPIN # %d P%d_%d - Slew? 0=fast, 1=slow: ",index+1,connector,pin);
			scanf(" %d",&slew);getchar();
		} while (slew < 0 || slew > 1);

		do {
			printf("\tPIN # %d P%d_%d - RX Enabled? 0=disabled, 1=enabled: ",index+1,connector,pin);
			scanf(" %d",&rx);getchar();
		} while (rx < 0 || rx > 1);

		do {
			printf("\tPIN # %d P%d_%d - Pullup or Pulldown? 0=pulldown, 1=pullup: ",index+1,connector,pin);
			scanf(" %d",&pull);getchar();
		} while (pull < 0 || pull > 1);

		do {
			printf("\tPIN # %d P%d_%d - Pull up-down Enabled? 0=enabled, 1=disabled: ",index+1,connector,pin);
			scanf(" %d",&pullEnabled);getchar();
		} while (pullEnabled < 0 || pullEnabled > 1);

		do {
			printf("\tPIN # %d P%d_%d - Pin Mux Mode? (0 through 7): ",index+1,connector,pin);
			scanf(" %d",&mux);getchar();
		} while (mux < 0 || mux > 7);

		unsigned char upper, lower;

		upper = 0;
		upper = (usage<<7) | (type<<5);

		lower = 0;
		lower = (slew<<6) | (rx<<5) | (pull<<4) | (pullEnabled<<3) | mux;

		eeprom[eepromIndex[connector-8][pin-1]] = upper;
		eeprom[eepromIndex[connector-8][pin-1]+1] = lower;
	}*/

	//  write data to file
	printf("\nCreating output file... ./data.eeprom\n\n");
	char *file = "data.eeprom";
	FILE *p = NULL;
	p = fopen(file, "w");
	if (p== NULL) {
		printf("Error in opening a file..", file);
		return(1);
	}
	fwrite(eeprom, sizeEEPROM, 1, p);
	fclose(p);

	return 0;
}

