#include <stdio.h>
#include <string.h>

#include "util.h"

static int fails;

static void show_hex(const char *s) {
	for (const unsigned char *p = (const unsigned char *)s; *p; p++) printf("%02x", *p);
}

static void expect(const char *label, const char *in, size_t cap, const char *want) {
	char out[512];
	if (cap > sizeof(out)) cap = sizeof(out);
	sanitize_filename(in, out, cap);

	int ok = strcmp(out, want) == 0;
	if (!ok) fails++;

	printf("%-34s %-28s %s\n", label, out, ok ? "ok" : "FAIL");
	if (!ok) {
		printf("%38sdapat ", "");
		show_hex(out);
		printf("\n%38smau   ", "");
		show_hex(want);
		printf("\n");
	}
}

/* Every byte of the result has to be a whole character: a name ending in half a
 * sequence is what Horizon rejects outright, and that is the bug this guards. */
static void expect_valid_utf8(const char *label, const char *in, size_t cap) {
	char out[512];
	if (cap > sizeof(out)) cap = sizeof(out);
	sanitize_filename(in, out, cap);

	const unsigned char *p = (const unsigned char *)out;
	int ok = 1;

	while (*p) {
		size_t len = *p < 0x80 ? 1
				: (*p & 0xE0) == 0xC0 ? 2
				: (*p & 0xF0) == 0xE0 ? 3
				: (*p & 0xF8) == 0xF0 ? 4
				: 0;
		if (len == 0) { ok = 0; break; }

		for (size_t i = 1; i < len; i++) {
			if ((p[i] & 0xC0) != 0x80) { ok = 0; break; }
		}
		if (!ok) break;
		p += len;
	}

	if (!ok) fails++;
	printf("%-34s cap=%-4zu utf8 %-17s %s\n", label, cap, ok ? "utuh" : "RUSAK",
			ok ? "ok" : "FAIL");
	if (!ok) {
		printf("%38shex   ", "");
		show_hex(out);
		printf("\n");
	}
}

int main(void) {
	const char *yoasobi = "YOASOBI「ハルカ」Official Music Video.mp4";

	printf("-- lolos apa adanya --\n");
	expect("ascii", "lagu.mp3", 128, "lagu.mp3");
	expect("jepang utuh", yoasobi, 128, yoasobi);
	expect("sirilik", "тест.txt", 128, "тест.txt");
	expect("aksen latin", "café.txt", 128, "café.txt");

	printf("\n-- karakter terlarang --\n");
	expect("pemisah path", "a/b\\c.txt", 128, "a_b_c.txt");
	expect("terlarang windows", "a:b*c?d\"e<f>g|h", 128, "a_b_c_d_e_f_g_h");
	expect("byte kontrol", "a\x01" "b.txt", 128, "a_b.txt");
	expect("naik direktori", "../../rahasia", 128, ".._.._rahasia");

	printf("\n-- ujung dipangkas --\n");
	expect("titik di ujung", "berkas...", 128, "berkas");
	expect("spasi di ujung", "berkas   ", 128, "berkas");
	expect("habis tak bersisa", "...", 128, "download");
	expect("kosong", "", 128, "download");

	printf("\n-- pemotongan di batas karakter --\n");
	/* "ハルカ" is three bytes per character, so a cap that lands inside one is where
	 * the old byte-wise copy produced a name Horizon would not accept. */
	expect("jepang, muat pas", "ハルカ", 10, "ハルカ");
	expect("jepang, potong 1", "ハルカ", 9, "ハル");
	expect("jepang, potong 2", "ハルカ", 8, "ハル");
	expect("jepang, potong 3", "ハルカ", 7, "ハル");
	expect("jepang, potong 4", "ハルカ", 6, "ハ");
	expect("cuma muat satu", "ハルカ", 4, "ハ");
	/* Nothing survives, and the fallback does not fit either, so it is cut like any
	 * other name. Still ASCII and still non-empty, which is all the caller needs;
	 * MAX_NAME is 256, so a buffer this small never actually happens. */
	expect("tak muat sama sekali", "ハルカ", 3, "do");

	printf("\n-- hasil selalu UTF-8 sah --\n");
	for (size_t cap = 1; cap <= 64; cap++) expect_valid_utf8("yoasobi", yoasobi, cap);

	printf("\n-- masukan rusak tidak menular --\n");
	/* A lone continuation byte and a lead byte with nothing after it both have to be
	 * dropped rather than copied through. */
	expect("byte lanjutan menyendiri", "\x80lagu", 128, "download");
	expect("lead tanpa lanjutan", "lagu\xe3", 128, "lagu");
	expect("urutan terpotong", "lagu\xe3\x81", 128, "lagu");

	printf("\n%s\n", fails ? "ADA YANG GAGAL" : "semua lolos");
	return fails ? 1 : 0;
}
