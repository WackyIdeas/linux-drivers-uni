/* Simple program to send ioctl commands to a device, from the command line
 * Usage: ioctl <device> <cmd>
 * Example: ioctl /dev/foo 3
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

int main (int argc, char *argv[])
{

	int file;
	int cmd; 

	char c;

	if (argc != 4) {
		fprintf(stderr, "ioctl: wrong number of arguments\n");
		printf("Simple program to send ioctl commands to a device, from the command line.\n");
		printf("Usage: ioctl <device> <cmd> <arg>\n");
		printf("Example: ioctl /dev/foo 0 w\n");
		exit(1);
	}

	sscanf (argv[2], "%i", &cmd);
	sscanf (argv[3], "%c", &c);

  	if ((file = open(argv[1], O_RDONLY)) < 0) {
		fprintf(stderr, "Error opening file %s\n", argv[1]);
		exit(1);
  	}

  	// Pass address of the character as ioctl will copy whatever is on that address from userspace
  	// to kernelspace
	if (ioctl(file, cmd, &c)) {
		fprintf(stderr, "Error sending the ioctl command %d to file %s\n", cmd, argv[1]);
		exit(1);
  	}
		
	close(file);
	return 0;
}
	
