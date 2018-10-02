#include <stdio.h>
#include <stdlib.h>
#include "pickle.h"

int main(int argc, char **argv) {
	struct picolInterp interp;
	picolInitInterp(&interp);
	picolRegisterCoreCommands(&interp);
	if (argc == 1) {
		while(1) {
			char clibuf[1024];
			int retcode;
			printf("picol> "); fflush(stdout);
			if (fgets(clibuf,1024,stdin) == NULL) return 0;
			retcode = picolEval(&interp,clibuf);
			if (interp.result[0] != '\0')
				printf("[%d] %s\n", retcode, interp.result);
		}
	} else if (argc == 2) {
		char buf[1024*16];
		FILE *fp = fopen(argv[1],"r");
		if (!fp) {
			perror("open"); exit(1);
		}
		buf[fread(buf,1,1024*16,fp)] = '\0';
		fclose(fp);
		if (picolEval(&interp,buf) != PICOL_OK) printf("%s\n", interp.result);
	}
	return 0;
}

