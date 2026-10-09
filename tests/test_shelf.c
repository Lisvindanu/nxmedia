#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shelf.h"

/*
 * The history and favourites lists, checked on the host against a scratch
 * directory. Persistence is where a mistake hides quietly: the app keeps working
 * and simply forgets, or remembers the wrong order, and nobody notices until the
 * list has been wrong for a week.
 */

static int fails;

static void check(const char *label, bool ok) {
	if (!ok) fails++;
	printf("%-44s %s\n", label, ok ? "ok" : "FAIL");
}

static MediaItem sample(const char *id, const char *title, int duration) {
	MediaItem item = {
		.id = strdup(id),
		.title = strdup(title),
		.filename = strdup(title),
		.author = strdup("Someone"),
		.views = 1234,
		.duration = duration,
	};
	return item;
}

static void free_sample(MediaItem *item) {
	media_item_free(item);
}

static const char *first_id(ShelfKind kind) {
	const MediaListing *list = shelf_list(kind);
	return list->count ? list->items[0].id : "(kosong)";
}

int main(void) {
	system("rm -rf /tmp/nxmedia-shelf-test");

	shelf_load();
	check("mulai kosong: riwayat", shelf_list(SHELF_HISTORY)->count == 0);
	check("mulai kosong: favorit", shelf_list(SHELF_FAVOURITES)->count == 0);

	MediaItem a = sample("aaa", "Lagu A", 200);
	MediaItem b = sample("bbb", "Lagu B", 300);

	printf("\n-- riwayat --\n");
	shelf_remember(&a);
	shelf_remember(&b);
	check("dua tersimpan", shelf_list(SHELF_HISTORY)->count == 2);
	check("yang terbaru di depan", strcmp(first_id(SHELF_HISTORY), "bbb") == 0);

	/* Playing something again should move it, not duplicate it: the list is places
	 * you have been, not a tally of how often. */
	shelf_remember(&a);
	check("diputar ulang tidak menggandakan", shelf_list(SHELF_HISTORY)->count == 2);
	check("diputar ulang pindah ke depan", strcmp(first_id(SHELF_HISTORY), "aaa") == 0);

	printf("\n-- batas --\n");
	for (int i = 0; i < 80; i++) {
		char id[16];
		snprintf(id, sizeof(id), "v%d", i);
		MediaItem filler = sample(id, id, 100);
		shelf_remember(&filler);
		free_sample(&filler);
	}
	check("riwayat berhenti di 60", shelf_list(SHELF_HISTORY)->count == 60);
	check("yang tertua terbuang", strcmp(first_id(SHELF_HISTORY), "v79") == 0);

	printf("\n-- favorit --\n");
	check("belum favorit", !shelf_is_favourite("aaa"));
	check("ditandai mengembalikan true", shelf_toggle_favourite(&a) == true);
	check("sekarang favorit", shelf_is_favourite("aaa"));
	check("dilepas mengembalikan false", shelf_toggle_favourite(&a) == false);
	check("sudah tidak favorit", !shelf_is_favourite("aaa"));

	shelf_toggle_favourite(&a);
	shelf_toggle_favourite(&b);
	check("dua favorit", shelf_list(SHELF_FAVOURITES)->count == 2);

	printf("\n-- bertahan setelah ditutup --\n");
	shelf_exit();
	shelf_load();

	check("riwayat kembali utuh", shelf_list(SHELF_HISTORY)->count == 60);
	check("urutan riwayat terjaga", strcmp(first_id(SHELF_HISTORY), "v79") == 0);
	check("favorit kembali utuh", shelf_list(SHELF_FAVOURITES)->count == 2);
	check("favorit masih dikenali", shelf_is_favourite("aaa") && shelf_is_favourite("bbb"));

	const MediaListing *fav = shelf_list(SHELF_FAVOURITES);
	const MediaItem *kept = &fav->items[0];
	check("judul ikut tersimpan", strcmp(kept->title, "Lagu B") == 0);
	check("durasi ikut tersimpan", kept->duration == 300);
	check("jumlah tayangan ikut tersimpan", kept->views == 1234);

	printf("\n-- posisi tontonan --\n");
	/* A long video, so the floor and the tail are far apart and each can be tested
	 * without the other interfering. */
	MediaItem c = sample("ccc", "Film Panjang", 3600);
	shelf_remember(&c);

	check("awalnya tidak ada posisi", shelf_position("ccc") == 0);

	/* Under the floor there is nothing worth returning to, so it is stored as a
	 * fresh start rather than as four seconds in. */
	shelf_remember_position("ccc", 4);
	check("di bawah 30s dianggap dari awal", shelf_position("ccc") == 0);

	shelf_remember_position("ccc", 754);
	check("posisi tersimpan", shelf_position("ccc") == 754);

	/* Inside the last fifteen seconds counts as watched through. */
	shelf_remember_position("ccc", 3590);
	check("di ekor dianggap tamat", shelf_position("ccc") == 0);

	shelf_remember_position("ccc", 754);
	shelf_remember_position("ccc", -5);
	check("angka negatif tidak merusak", shelf_position("ccc") == 0);

	shelf_remember_position("ccc", 900);
	check("id yang tidak ada diabaikan", (shelf_remember_position("zzz", 100), true));
	check("posisi id lain tidak tersentuh", shelf_position("ccc") == 900);

	/* The bug this guards: recording a play replaces the entry, and the position
	 * lives only in the entry being replaced. */
	shelf_remember(&c);
	check("diputar ulang tidak menghapus posisi", shelf_position("ccc") == 900);
	check("diputar ulang tetap pindah ke depan", strcmp(first_id(SHELF_HISTORY), "ccc") == 0);

	shelf_exit();
	shelf_load();
	check("posisi bertahan setelah ditutup", shelf_position("ccc") == 900);

	free_sample(&c);

	free_sample(&a);
	free_sample(&b);
	shelf_exit();
	system("rm -rf /tmp/nxmedia-shelf-test");

	printf("\n%s\n", fails ? "ADA YANG GAGAL" : "semua lolos");
	return fails ? 1 : 0;
}
