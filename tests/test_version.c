#include <stdio.h>
#include "util.h"

static int fails;

static void expect(const char *candidate, const char *current, bool want) {
	bool got = version_is_newer(candidate, current);
	int ok = got == want;
	if (!ok) fails++;
	printf("%-12s > %-12s  %-5s want %-5s %s\n", candidate, current,
			got ? "yes" : "no", want ? "yes" : "no", ok ? "ok" : "FAIL");
}

int main(void) {
	expect("v0.2.0", "0.1.0", true);
	expect("0.2.0", "0.1.0", true);
	expect("v0.1.1", "0.1.0", true);
	expect("v1.0.0", "0.9.9", true);
	expect("v0.10.0", "0.9.0", true);

	expect("v0.1.0", "0.1.0", false);
	expect("v0.0.9", "0.1.0", false);
	expect("v0.1.0", "0.2.0", false);

	/* A release someone tagged by hand must never look like an upgrade. */
	expect("latest", "0.1.0", false);
	expect("", "0.1.0", false);
	expect("v", "0.1.0", false);
	expect("nxmedia-v0.9.0", "0.1.0", false);

	/* Short tags count the missing parts as zero. */
	expect("v1", "0.9.9", true);
	expect("v0.1", "0.1.0", false);

	printf("\n%s\n", fails ? "ADA YANG GAGAL" : "semua lolos");
	return fails ? 1 : 0;
}
