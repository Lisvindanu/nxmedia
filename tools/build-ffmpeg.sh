#!/usr/bin/env bash
# Builds ffmpeg for the Switch into ext/ffmpeg.
#
# The devkitPro switch-ffmpeg package cannot be used: its configure enables the
# "tls" protocol but not "https", which are separate entries in ffmpeg, so it can
# only open http:// URLs.
#
# Only the codecs this app actually plays are built. The list is long because
# "local video file" covers whatever the user dropped on the card, but it is still
# a fraction of a default build.
set -euo pipefail

VERSION=7.1
DEVKITPRO=${DEVKITPRO:-/opt/devkitpro}
PORTLIBS=$DEVKITPRO/portlibs/switch
PROJECT=$(cd "$(dirname "$0")/.." && pwd)
PREFIX=$PROJECT/ext/ffmpeg
WORK=${FFMPEG_WORK:-/tmp/nxmedia-ffmpeg}

export PATH=$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH

mkdir -p "$WORK"
cd "$WORK"

if [ ! -d "ffmpeg-$VERSION" ]; then
	[ -f "ffmpeg-$VERSION.tar.xz" ] || curl -fL -o "ffmpeg-$VERSION.tar.xz" \
		"https://ffmpeg.org/releases/ffmpeg-$VERSION.tar.xz"
	tar xf "ffmpeg-$VERSION.tar.xz"

	base=https://raw.githubusercontent.com/devkitPro/pacman-packages/master/switch/ffmpeg
	curl -fL -o "ffmpeg-$VERSION.patch" "$base/ffmpeg-$VERSION.patch"
	curl -fL -o tls.patch "$base/tls.patch"

	cd "ffmpeg-$VERSION"
	# The first patch teaches configure the horizon target and adds the nvtegra
	# hardware decoder; the second adds libnx as a TLS backend, which is what lets
	# the https protocol exist at all.
	patch -Np1 -i "../ffmpeg-$VERSION.patch"
	patch -Np1 -i ../tls.patch
	cd ..
fi

cd "ffmpeg-$VERSION"

# Cortex-A57 cannot software-decode 1080p, so nvtegra -- the Tegra X1's NVDEC block
# -- is the whole reason video is viable here. It is marked gpl in configure, hence
# --enable-gpl.
HWACCELS=h264_nvtegra,hevc_nvtegra,vp8_nvtegra,vp9_nvtegra,mpeg2_nvtegra,mpeg4_nvtegra,vc1_nvtegra,wmv3_nvtegra,mjpeg_nvtegra

# Everything NVDEC handles, plus the software fallbacks for formats it does not.
VIDEO_DECODERS=h264,hevc,vp8,vp9,mpeg4,mpeg2video,mpeg1video,vc1,wmv3,mjpeg,theora
AUDIO_DECODERS=aac,aac_latm,mp3,mp3float,ac3,eac3,dca,opus,vorbis,flac,alac,wmav2,pcm_s16le,pcm_s16be,pcm_u8,pcm_f32le

# crypto and data carry AES-128 HLS: IPTV playlists routinely encrypt their segments
# and hand out the key as a data: URI.
PROTOCOLS=file,http,https,tcp,tls,crypto,data,hls

# hls leans on mpegts and mov for its segments, so both stay even for pure streaming.
DEMUXERS=hls,mpegts,mov,matroska,avi,flv,asf,mp3,aac,ogg,flac,wav,aiff,h264,hevc,mpegvideo,live_flv

PARSERS=h264,hevc,vp8,vp9,mpeg4video,mpegvideo,vc1,mjpeg,aac,aac_latm,mpegaudio,ac3,opus,vorbis,flac,dca

# The mp4-to-annexb pair is mandatory: mp4 and fmp4 store parameter sets in the
# container, and the hardware decoder only reads them in-band.
BSFS=h264_mp4toannexb,hevc_mp4toannexb,aac_adtstoasc,extract_extradata,vp9_superframe_split

OPTIONS=(
	--prefix="$PREFIX"
	--disable-shared --enable-static --enable-pic
	--cross-prefix=aarch64-none-elf- --enable-cross-compile
	--arch=aarch64 --cpu=cortex-a57 --target-os=horizon
	--extra-cflags="-D__SWITCH__ -D_GNU_SOURCE -O2 -march=armv8-a -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec -I$PORTLIBS/include -I$DEVKITPRO/libnx/include"
	--extra-ldflags="-fPIE -L$PORTLIBS/lib -L$DEVKITPRO/libnx/lib"
	--disable-runtime-cpudetect --disable-programs --disable-debug
	--disable-doc --disable-autodetect
	--enable-asm --enable-neon
	--disable-avdevice --disable-avfilter --disable-postproc
	--enable-swscale --enable-swresample --enable-network --enable-libnx
	--enable-gpl --enable-nvtegra
	--disable-everything
	--enable-hwaccel="$HWACCELS"
	--enable-protocol="$PROTOCOLS"
	--enable-demuxer="$DEMUXERS"
	--enable-decoder="$VIDEO_DECODERS,$AUDIO_DECODERS"
	--enable-parser="$PARSERS"
	--enable-bsf="$BSFS"
	--enable-zlib
)

# configure is slow and make tracks neither it nor its arguments, so the options are
# fingerprinted. Editing the lists above is what a rebuild has to notice.
STAMP=.nxmedia-configure
printf '%s\n' "${OPTIONS[@]}" > "$STAMP.new"
if [ ! -f config.h ] || ! cmp -s "$STAMP" "$STAMP.new"; then
	[ -f config.h ] && make distclean >/dev/null 2>&1 || true
	./configure "${OPTIONS[@]}"
	mv "$STAMP.new" "$STAMP"
else
	rm -f "$STAMP.new"
fi

make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
make install

printf '\nInstalled to %s\n' "$PREFIX"
du -sh "$PREFIX/lib"
