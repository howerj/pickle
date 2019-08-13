#include "pickle.h"
#include <stdio.h>
#include <string.h>

int main(void) {
	pickle_t *p = NULL;
	if (pickle_new(&p, NULL) < 0)
		return -1;
	const char *prompt = "> ";
	fputs(prompt, stdout);
	fflush(stdout);
	for (char buf[80] = { 0 }; fgets(buf, sizeof buf, stdin); memset(buf, 0, sizeof buf)) {
		const char *r = NULL;
		const int er = pickle_eval(p, buf);
		pickle_get_result_string(p, &r);
		fprintf(stdout, "[%d]: %s\n%s", er, r, prompt);
		fflush(stdout);
	}
	return pickle_delete(p);
}
