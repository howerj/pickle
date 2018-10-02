#include "pickle.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#define FILE_SZ (1024 * 16)

int main(int argc, char **argv) {
	int r = 0;
	FILE *input = stdin, *output = stdout;
	pickle_t interp = { .initialized = 0 };
	pickle_initialize(&interp);
	if (argc == 1) {
		for(;;) {
			char clibuf[1024];
			fprintf(output, "pickle> "); 
			fflush(output);
			if (!fgets(clibuf, sizeof clibuf, input))
				goto end;
			const int retcode = pickle_eval(&interp,clibuf);
			if (interp.result[0] != '\0')
				fprintf(output, "[%d] %s\n", retcode, interp.result);
		}
	}
 	if(argc != 2) {
		fprintf(stderr, "usage: %s file\n", argv[0]);
		r = -1;
		goto end;
	}	
	FILE *fp = fopen(argv[1], "r");
	if (!fp) {
		fprintf(stderr, "failed to open file %s: %s\n", argv[1], strerror(errno));
		r = -1;
		goto end;
	}
	char buf[1024*16];
	buf[fread(buf, 1, 1024*16, fp)] = '\0';
	fclose(fp);
	if (pickle_eval(&interp, buf) != 0) 
		fprintf(output, "%s\n", interp.result);
end:
	pickle_deinitialize(&interp);
	return r;
}

