/*
 *
 *  Copyright(C) 2018 Gerald Coe, Devantech Ltd <gerry@devantech.co.uk>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any purpose with or
 *  without fee is hereby granted, provided that the above copyright notice and
 *  this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO
 *  THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *  DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 *  AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 *  CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>


enum cmds
{
    DONE = 0xb0, GET_VER, RESET_FPGA, ERASE_CHIP, ERASE_64k, PROG_PAGE, READ_PAGE, VERIFY_PAGE, GET_CDONE, RELEASE_FPGA
};

#define FLASHSIZE 1048576	// 1MByte because that is the size of the Flash chip
unsigned char FPGAbuf[FLASHSIZE];
unsigned char SerBuf[300];
char ProgName[30];
int fd;
char verify;
int rw_offset = 0;

static void help(const char *progname)
{
	fprintf(stderr, "Programming tool for Devantech iceFUN board.\n");
	fprintf(stderr, "Usage: %s [-P] <SerialPort>\n", progname);
	fprintf(stderr, "       %s <input file>\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -P <Serial Port>      use the specified USB device [default: /dev/ttyACM0]\n");
	fprintf(stderr, "  -h                    display this help and exit\n");
	fprintf(stderr, "  -o <offset in bytes>  start address for write [default: 0]\n");
	fprintf(stderr, "                        (append 'k' to the argument for size in kilobytes,\n");
	fprintf(stderr, "                         or 'M' for size in megabytes)\n");
	fprintf(stderr, "                         or prefix '0x' to the argument for size in hexdecimal\n");
	fprintf(stderr, "  --help\n");
	fprintf(stderr, "  -v                    skip verification\n");
	fprintf(stderr, "Example:\n");
	fprintf(stderr, "%s -P /dev/ttyACM1 blinky.bin\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Exit status:\n");
	fprintf(stderr, "  0 on success,\n");
	fprintf(stderr, "  1 operation failed.\n");
	fprintf(stderr, "\n");
}

int GetVersion(void)								// print firmware version number
{
	SerBuf[0] = GET_VER;
	write(fd, SerBuf, 1);
//	tcdrain(fd);
	read(fd, SerBuf, 2);
	if(SerBuf[0] == 38) {
		fprintf(stderr, "iceFUN v%d, ",SerBuf[1]);
		return 0;
	}
	else {
		fprintf(stderr, "%s: Error getting Version\n", ProgName);
		return EXIT_FAILURE;
	}
}

int resetFPGA(void)									// reset the FPGA and return the flash ID bytes
{
	SerBuf[0] = RESET_FPGA;
	write(fd, SerBuf, 1);
	read(fd, SerBuf, 3);
	fprintf(stderr, "Flash ID %02X %02X %02X\n",SerBuf[0], SerBuf[1], SerBuf[2]);
	return 0;
}

int releaseFPGA(void)								// run the FPGA
{
	SerBuf[0] = RELEASE_FPGA;
	write(fd, SerBuf, 1);
	read(fd, SerBuf, 1);
	fprintf(stderr, "Done.\n");
	return 0;
}


int main(int argc, char **argv)
{
const char *portPath = "/dev/serial/by-id/";
const char *filename = NULL;
char portName[300];
int length;
struct termios config;

	verify = 1;										// verify unless told not to
 	DIR * d = opendir(portPath); 						// open the path
	struct dirent *dir; 							// for the directory entries
	if(d==NULL) strcpy(portName, "/dev/ttyACM0");		// default serial port to use
	else {
		while ((dir = readdir(d)) != NULL) 			// if we were able to read somehting from the directory
		{
			char *pF = strstr(dir->d_name, "iceFUN");
			if(pF) {
				strcpy(portName, portPath);			// found iceFUN board so copy full symlink
				strcat(portName, dir->d_name);
				break;
			}
		}
	}
	closedir(d);

	fd = open(portName, O_RDWR | O_NOCTTY);
	if(fd == -1) {
		help(argv[0]);
		fprintf(stderr, "%s: failed to open serial port.\n", argv[0]);
		return EXIT_FAILURE;
	}
	tcgetattr(fd, &config);
	cfmakeraw(&config);								// set options for raw data
	tcsetattr(fd, TCSANOW, &config);

/* Decode command line parameters */
	static struct option long_options[] = {
		{"help", no_argument, NULL, -2},
		{NULL, 0, NULL, 0}
	};

	int opt;
  char *endptr;
	while ((opt = getopt_long(argc, argv, "P:o:vh", long_options, NULL)) != -1) {
		switch (opt) {
		case 'P': /* Serial port */
			strcpy(portName, optarg);
			break;
		case 'h':
		case -2:
			help(argv[0]);
			close(fd);
			return EXIT_SUCCESS;
		case 'v':
			verify = 0;
			break;
    case 'o': /* set address offset */
      rw_offset = strtol(optarg, &endptr, 0);
      if (*endptr == '\0')
        /* ok */;
      else if (!strcmp(endptr, "k"))
        rw_offset *= 1024;
      else if (!strcmp(endptr, "M"))
        rw_offset *= 1024 * 1024;
      else {
        fprintf(stderr, "'%s' is not a valid offset\n", optarg);
        return EXIT_FAILURE;
      }
      break;
		default:
			/* error message has already been printed */
			fprintf(stderr, "Try `%s -h' for more information.\n", argv[0]);
			close(fd);
			return EXIT_FAILURE;
		}
	}
	if (optind + 1 == argc) {
		filename = argv[optind];
	} else if (optind != argc) {
		fprintf(stderr, "%s: too many arguments\n", argv[0]);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
		close(fd);
		return EXIT_FAILURE;
	} else  {
		fprintf(stderr, "%s: missing argument\n", argv[0]);
		fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);
		close(fd);
		return EXIT_FAILURE;
	}

	FILE* fp = fopen(filename, "rb");
	if(fp==NULL) {
		fprintf(stderr, "%s: failed to open file %s.\n", argv[0], filename);
		close(fd);
		return EXIT_FAILURE;
	}
	length = fread(&FPGAbuf[rw_offset], 1, FLASHSIZE, fp);

	strcpy(ProgName, argv[0]);
	if(!GetVersion()) resetFPGA();						// reset the FPGA
	else return EXIT_FAILURE;

  int endPage = ((rw_offset + length) >> 16) + 1;
  for (int page = (rw_offset >> 16); page < endPage; page++)			// erase sufficient 64k sectors
  {
    SerBuf[0] = ERASE_64k;
    SerBuf[1] = page;
    write(fd, SerBuf, 2);
    fprintf(stderr,"Erasing sector %02X0000\n", page);
    read(fd, SerBuf, 1);
  }
  fprintf(stderr, "file size: %d\n", (int)length);

	int addr = rw_offset;
	fprintf(stderr,"Programming ");						// program the FPGA
	int cnt = 0;
  int endAddr = addr + length; // no flashsize check
	while (addr < endAddr)
	{
		SerBuf[0] = PROG_PAGE;
		SerBuf[1] = (addr>>16);
		SerBuf[2] = (addr>>8);
		SerBuf[3] = (addr);
		for (int x = 0; x < 256; x++) SerBuf[x + 4] = FPGAbuf[addr++];
		write(fd, SerBuf, 260);
		read(fd, SerBuf, 4);
		if (SerBuf[0] != 0)
		{
			fprintf(stderr,"\nProgram failed at %06X, %02X expected, %02X read.\n", addr - 256 + SerBuf[1] - 4, SerBuf[2], SerBuf[3]);
			return EXIT_FAILURE;
		}
		if (++cnt == 10)
		{
			cnt = 0;
			fprintf(stderr,".");
		}
	}

	if(verify) {
		addr = rw_offset;
		fprintf(stderr,"\nVerifying ");
		cnt = 0;
		while (addr < endAddr)
		{
			SerBuf[0] = VERIFY_PAGE;
			SerBuf[1] = (addr >> 16);
			SerBuf[2] = (addr >> 8);
			SerBuf[3] = addr;
			for (int x = 0; x < 256; x++) SerBuf[x + 4] = FPGAbuf[addr++];
			write(fd, SerBuf, 260);
			read(fd, SerBuf, 4);
			if (SerBuf[0] > 0)
			{
				fprintf(stderr,"\nVerify failed at %06X, %02X expected, %02X read.\n", addr - 256 + SerBuf[1] - 4, SerBuf[2], SerBuf[3]);
				return EXIT_FAILURE;
			}
			if (++cnt == 10)
			{
				cnt = 0;
				fprintf(stderr,".");
			}
		}
	}
	fprintf(stderr,"\n");

	releaseFPGA();
	return 0;
}


