#include "i18n.h"

#include <stdio.h>
#include <string.h>

#include "util.h"

#define CONFIG_DIR "sdmc:/switch/nxmedia"
#define CONFIG_PATH CONFIG_DIR "/lang.txt"

static Lang current = LANG_EN;

static const char *const STRINGS[STR_COUNT][LANG_COUNT] = {
	[STR_RADIO] = { "Radio", "Radio" },
	[STR_RADIO_BLURB] = { "spin the globe, pick a country, listen",
			"putar bola dunia, pilih negara, dengar" },
	[STR_MUSIC] = { "Music", "Musik" },
	[STR_MUSIC_BLURB] = { "songs on the SD card", "lagu di kartu SD" },
	[STR_VIDEO] = { "Video", "Video" },
	[STR_VIDEO_BLURB] = { "live tv and files on the card", "tv live dan berkas di kartu" },
	[STR_SETTINGS] = { "Settings", "Pengaturan" },
	[STR_SETTINGS_BLURB] = { "language and options", "bahasa dan opsi" },

	[STR_HOME_EYEBROW] = { "home", "beranda" },
	[STR_RADIO_EYEBROW] = { "radio", "radio" },
	[STR_SETTINGS_EYEBROW] = { "settings", "pengaturan" },
	[STR_VIDEO_EYEBROW] = { "video", "video" },
	[STR_MUSIC_EYEBROW] = { "music", "musik" },
	[STR_LIVE_EYEBROW] = { "live tv", "tv live" },
	[STR_SOON] = { "soon", "segera" },

	[STR_OPEN] = { "Open", "Buka" },
	[STR_LISTEN] = { "Listen", "Dengar" },
	[STR_PLAY] = { "Play", "Putar" },
	[STR_WATCH] = { "Watch", "Tonton" },
	[STR_PAUSE] = { "Pause", "Jeda" },
	[STR_RESUME] = { "Resume", "Lanjut" },
	[STR_UP] = { "Up", "Naik" },
	[STR_BACK] = { "Back", "Kembali" },
	[STR_REFRESH] = { "Refresh", "Muat ulang" },
	[STR_STOP] = { "Stop", "Stop" },
	[STR_SCREEN_OFF] = { "Screen off", "Layar mati" },
	[STR_HOME] = { "Home", "Beranda" },
	[STR_QUIT] = { "Quit", "Keluar" },
	[STR_SWITCH] = { "Switch", "Ganti" },

	[STR_KEY_STICK] = { "stick", "stik" },
	[STR_KEY_TOUCH] = { "touch", "sentuh" },
	[STR_KEY_ZOOM] = { "ZL ZR", "ZL ZR" },
	[STR_KEY_DPAD] = { "D-pad", "D-pad" },
	[STR_DO_SPIN] = { "spin the globe", "putar bola" },
	[STR_DO_PICK] = { "spin, pick", "putar, pilih" },
	[STR_DO_ZOOM] = { "zoom in", "perbesar" },
	[STR_DO_STATION] = { "change station", "ganti stasiun" },

	[STR_PICK_COUNTRY] = { "Pick a country", "Pilih negara" },
	[STR_RADIO_PROMPT] = { "spin the globe, touch a country",
			"putar bola dunia, sentuh sebuah negara" },
	[STR_SCREEN_OFF_NOTE] = { "screen off, audio still running", "layar mati, audio jalan" },
	[STR_LOADING_STATIONS] = { "Loading stations", "Memuat stasiun" },
	[STR_NO_STATIONS] = { "no MP3 stations here", "tidak ada stasiun MP3 di sini" },
	[STR_READY] = { "ready", "siap" },
	[STR_CONNECTING] = { "connecting", "menyambung" },
	[STR_PLAYING] = { "playing", "diputar" },

	[STR_BROWSE_HINT] = { "A opens, B goes up", "A membuka, B naik" },
	[STR_NO_MEDIA] = { "nothing playable in this folder",
			"tidak ada yang bisa diputar di folder ini" },
	[STR_KIND_FOLDER] = { "folder", "folder" },
	[STR_KIND_VIDEO] = { "video", "video" },
	[STR_KIND_AUDIO] = { "audio", "audio" },
	[STR_KIND_LIVE] = { "live", "live" },
	[STR_KIND_SD] = { "SD", "SD" },

	[STR_PICK_SOURCE] = { "pick where to watch from", "pilih mau nonton dari mana" },
	[STR_WATCH_LIVE] = { "Live TV", "TV Live" },
	[STR_WATCH_LIVE_NOTE] = { "Indonesian channels", "saluran Indonesia" },
	[STR_WATCH_CARD] = { "On the card", "Di kartu" },
	[STR_WATCH_CARD_NOTE] = { "video files", "berkas video" },
	[STR_LIVE_HINT] = { "A watches, Y reloads the list",
			"A menonton, Y memuat ulang daftar" },
	[STR_LOADING_CHANNELS] = { "Loading channels", "Memuat saluran" },
	[STR_NO_CHANNELS] = { "no channels to show", "tidak ada saluran" },

	[STR_LANGUAGE] = { "Language", "Bahasa" },
	[STR_SETTINGS_HINT] = { "up and down to choose, A to act",
			"atas bawah untuk memilih, A untuk menjalankan" },

	[STR_VERSION] = { "Version", "Versi" },
	[STR_UPDATE] = { "Update", "Pembaruan" },
	[STR_CHECK_UPDATE] = { "Check", "Cek" },
	[STR_CHECKING] = { "checking", "memeriksa" },
	[STR_UP_TO_DATE] = { "up to date", "sudah terbaru" },
	[STR_UPDATE_READY] = { "press A to install", "tekan A untuk memasang" },
	[STR_DOWNLOADING] = { "downloading", "mengunduh" },
	[STR_UPDATE_DONE] = { "installed, quit to finish", "terpasang, keluar untuk selesai" },
	[STR_INSTALL] = { "Install", "Pasang" },

	[STR_LOADING_MAP] = { "Loading world map", "Memuat peta dunia" },
	[STR_ROMFS_FAIL] = { "Could not open romfs", "Gagal membuka romfs" },
	[STR_MAP_FAIL] = { "World map failed to load", "Peta dunia gagal dimuat" },
	[STR_NET_FAIL] = { "Could not set up networking", "Gagal menyiapkan jaringan" },
	[STR_NET_FAIL_WHY] = { "socketInitialize refused by the system",
			"socketInitialize ditolak sistem" },
	[STR_AUDIO_FAIL] = { "Could not set up audio", "Gagal menyiapkan audio" },

	[STR_YT] = { "YouTube", "YouTube" },
	[STR_YT_BLURB] = { "search, play, save", "cari, putar, simpan" },
	[STR_YT_EYEBROW] = { "from the server", "lewat server" },
	[STR_YT_EMPTY] = { "Nothing came back. Press Y to search.", "Tidak ada yang kembali. Tekan Y untuk mencari." },
	[STR_SEARCH] = { "search", "cari" },
	[STR_SEARCH_HEADER] = { "Search YouTube", "Cari di YouTube" },
	[STR_SEARCHING] = { "Searching", "Mencari" },
	[STR_SAVE] = { "save", "simpan" },
	[STR_TRENDING] = { "Trending", "Populer" },
	[STR_HISTORY] = { "History", "Riwayat" },
	[STR_FAVOURITES] = { "Saved", "Favorit" },
	[STR_UNFAVOURITE] = { "unsave", "lepas" },
	[STR_TAB] = { "tab", "tab" },
	[STR_FAVOURITE_ADD] = { "Added to saved", "Ditambahkan ke favorit" },
	[STR_FAVOURITE_DROP] = { "Removed from saved", "Dihapus dari favorit" },
	[STR_SHELF_EMPTY] = { "Nothing here yet", "Belum ada apa-apa di sini" },
	[STR_LIVE_CHIP] = { "LIVE", "LIVE" },
	[STR_LIVE_NO_SUPPORT] = { "The server cannot fetch a live stream yet",
			"Server belum bisa mengambil siaran langsung" },
	[STR_PREPARING] = { "Preparing on the server", "Disiapkan server" },
	[STR_SAVED] = { "Saved to the card", "Tersimpan ke kartu" },
	[STR_SAVE_FAIL] = { "Could not save", "Gagal menyimpan" },
};

static const char *const NAMES[LANG_COUNT] = { "English", "Bahasa Indonesia" };

void lang_load(void) {
	FILE *file = fopen(CONFIG_PATH, "r");
	if (!file) return;

	char line[16] = {0};
	if (fgets(line, sizeof(line), file) && strncmp(line, "id", 2) == 0) current = LANG_ID;
	fclose(file);
}

void lang_set(Lang lang) {
	if (lang >= LANG_COUNT) return;
	current = lang;

	/* Losing the preference is not worth interrupting anyone over: the app still
	 * works, it just asks again next launch. */
	if (!mkdir_p(CONFIG_DIR)) return;

	FILE *file = fopen(CONFIG_PATH, "w");
	if (!file) return;

	fputs(lang == LANG_ID ? "id\n" : "en\n", file);
	fclose(file);
}

Lang lang_get(void) {
	return current;
}

const char *lang_name(Lang lang) {
	return lang < LANG_COUNT ? NAMES[lang] : NAMES[LANG_EN];
}

const char *T(StringId id) {
	return id < STR_COUNT ? STRINGS[id][current] : "";
}
