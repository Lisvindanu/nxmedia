#include <stdio.h>
#include <string.h>
#include "world.h"

static int fails;

static void expect(const char *label, float lon, float lat, const char *want) {
	size_t i = world_locate(lon, lat);
	const WorldCountry *c = world_country(i);
	const char *got = c ? c->iso : "--";
	int ok = strcmp(got, want) == 0;
	if (!ok) fails++;
	printf("%-22s %-3s want %-3s %s  (%s)\n", label, got, want, ok ? "ok" : "FAIL",
			c ? c->name : "laut lepas");
}

int main(void) {
	char err[256];
	if (!world_load("romfs/world.bin", err, sizeof(err))) {
		printf("load: %s\n", err);
		return 1;
	}
	printf("%zu negara dimuat\n\n", world_country_count());

	expect("Jakarta",       106.85f,  -6.21f, "ID");
	expect("Tokyo",         139.69f,  35.69f, "JP");
	expect("Paris",           2.35f,  48.86f, "FR");
	expect("Nairobi",        36.82f,  -1.29f, "KE");
	expect("Sao Paulo",     -46.63f, -23.55f, "BR");
	expect("Denver",       -104.99f,  39.74f, "US");
	expect("Tengah Pasifik",-140.00f,  -5.00f, "--");
	expect("Atlantik",      -30.00f,  20.00f, "--");
	expect("Kutub selatan",   0.00f, -85.00f, "AQ");

	size_t id = world_find_iso("id");
	const WorldCountry *c = world_country(id);
	printf("\nfind_iso(id) -> %s, kamera ke %.2f,%.2f\n", c ? c->name : "?", c->lat, c->lon);

	world_free();
	printf("\n%s\n", fails ? "ADA YANG GAGAL" : "semua lolos");
	return fails != 0;
}
