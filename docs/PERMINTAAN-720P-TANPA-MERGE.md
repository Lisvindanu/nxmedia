# Permintaan ke MediaVault: 720p tanpa merge

Ditulis 2026-10-10 untuk agen yang memegang `mediavault-laravel`.
Klien: nxmedia 1.4.0 di Nintendo Switch.

## Masalahnya, dari sisi klien

nxmedia punya dua jalur menonton, dan jaraknya terlalu jauh:

| Jalur | Endpoint | Tunggu | Kualitas |
|---|---|---|---|
| Tonton | `/proxy/{id}` | 8–25 detik, **tidak tumbuh dengan panjang video** | 360p |
| Simpan | `/download/{id}` | **≈ 0,3 × durasi** (terukur: 60 s untuk 3m33s, 184 s untuk 10 menit) | 1080p |

Jalur pertama instan karena YouTube sudah menyediakan **itag 18** — 360p dengan
video dan audio menyatu dalam satu berkas. Tidak ada yang perlu digabung.

Jalur kedua lambat karena 1080p hanya ada sebagai video-saja dan audio-saja, jadi
server harus menjalankan ffmpeg untuk menyatukannya sebelum byte pertama keluar.

Akibatnya di konsol: 360p terlihat kasar di layar 1080p dock, tapi tidak ada yang
mau menunggu tiga menit untuk video sepuluh menit. Jadi praktis semua tontonan
terjadi di 360p.

## Yang diminta

**Periksa itag 22 sebelum jatuh ke merge.**

itag 22 adalah 720p dengan audio sudah menyatu, sama seperti itag 18. Kalau ada,
ia bisa diproksikan langsung dengan biaya yang sama dengan 360p — nol merge, dan
waktu tunggunya tidak tumbuh dengan panjang video.

Secara yt-dlp:

```
# yang sekarang dipakai jalur tonton
-f 18

# yang diminta: 720p dulu, 360p kalau tidak ada
-f 22/18
```

Catatan penting: **itag 22 tidak selalu ada.** YouTube tidak menyediakannya untuk
semua video, dan untuk sebagian hanya untuk beberapa waktu setelah diunggah. Jadi
ini harus fallback, bukan pengganti. `22/18` sudah menangani itu sendiri.

## Bentuk API yang diharapkan

Tidak perlu endpoint baru. Dua pilihan, yang pertama lebih disukai:

**Pilihan A — `/proxy/{id}` naik sendiri.** Tidak ada perubahan di klien sama
sekali; nxmedia yang sudah terpasang di kartu orang langsung dapat 720p.

**Pilihan B — parameter opsional.** `/proxy/{id}?quality=720` dengan default tetap
360p. Butuh rilis klien baru, tapi aman untuk klien lama.

## Yang perlu dipastikan sebelum dikirim

1. **`Accept-Ranges: bytes` harus tetap ada.** Scrub bar dan lompat-lompat di
   nxmedia bergantung pada byte range. Tanpa itu videonya jalan tapi tidak bisa
   digeser, dan itu kemunduran dari 360p yang sekarang.
2. **Byte pertama harus keluar di bawah 15 detik.** `rw_timeout` ffmpeg di
   `player.c` adalah 15 detik. Di atas itu player menyerah sebelum server
   menjawab. nxmedia memang menghangatkan proxy lebih dulu lewat `http_touch`
   dengan timeout 60 detik, jadi ada kelonggaran — tapi jangan diandalkan.
3. **URL googlevideo terikat IP yang mengekstraknya.** Mesin yang me-resolve harus
   mesin yang menyajikan byte-nya. Ini sebabnya streaming tidak bisa pindah ke
   edge worker, dan itu tidak berubah untuk 720p.

## Cara memastikan ini berhasil

Ukur dari konsol, bukan dari server:

```
# waktu sampai byte pertama
curl -o /dev/null -s -w 'TTFB %{time_starttransfer}s  total %{time_total}s\n' \
     -r 0-1024 'https://mediavault.project-n.site/api/public/proxy/<id>'

# byte range benar-benar dilayani
curl -sI -r 100-200 'https://.../proxy/<id>' | grep -i 'content-range\|accept-ranges'
```

Target: TTFB tetap di bawah 25 detik untuk video dingin, dan **tidak berubah**
antara video 3 menit dan video 1 jam. Kalau TTFB mulai naik seiring durasi, itu
tanda merge masih jalan dan itag 22 tidak terpakai.
