#!/usr/bin/env python3
"""Probes every channel in an m3u the way the Switch build would open it.

The console's ffmpeg is built with a deliberately narrow set of protocols, demuxers
and decoders (see build-ffmpeg.sh). A channel that plays fine in VLC can still be
unopenable there, so reachability alone is not the question being asked: each URL is
decode-probed and the result checked against what the console can actually handle.
"""

import argparse
import concurrent.futures
import json
import re
import subprocess
import sys
import urllib.request

# Kept in step with tools/build-ffmpeg.sh by hand. A name outside these sets means the
# stream opens on a desktop and shows nothing on the console.
PROTOCOLS = {"file", "http", "https", "tcp", "tls", "crypto", "data", "hls"}
VIDEO_DECODERS = {"h264", "hevc", "vp8", "vp9", "mpeg4", "mpeg2video", "mpeg1video",
                  "vc1", "wmv3", "mjpeg", "theora"}
AUDIO_DECODERS = {"aac", "aac_latm", "mp3", "mp3float", "ac3", "eac3", "dca", "opus",
                  "vorbis", "flac", "alac", "wmav2", "pcm_s16le", "pcm_s16be", "pcm_u8",
                  "pcm_f32le"}
# ffprobe reports container names in comma-separated families; these are the ones the
# enabled demuxer list covers.
DEMUXERS = {"hls", "mpegts", "mov", "mp4", "m4a", "3gp", "3g2", "mj2", "matroska",
            "webm", "avi", "flv", "asf", "mp3", "aac", "ogg", "flac", "wav", "aiff",
            "h264", "hevc", "mpegvideo", "live_flv"}

UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36"

ATTR = re.compile(r'([A-Za-z0-9_-]+)="([^"]*)"')


def parse(text):
    """Mirrors source/m3u.c: attributes off #EXTINF, plus the #EXTVLCOPT overrides."""
    out, pending = [], None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#EXTINF:"):
            attrs = dict(ATTR.findall(line))
            # The name is whatever follows the last quoted attribute.
            quoted, comma = False, None
            for i, ch in enumerate(line):
                if ch == '"':
                    quoted = not quoted
                elif ch == "," and not quoted:
                    comma = i
                    break
            pending = {
                "name": line[comma + 1:].strip() if comma is not None else "",
                "group": attrs.get("group-title", ""),
                "referer": attrs.get("http-referrer", ""),
                "user_agent": attrs.get("user-agent", ""),
            }
        elif line.startswith("#EXTVLCOPT:http-referrer="):
            if pending:
                pending["referer"] = line.split("=", 1)[1]
        elif line.startswith("#EXTVLCOPT:http-user-agent="):
            if pending:
                pending["user_agent"] = line.split("=", 1)[1]
        elif line.startswith("#"):
            continue
        elif pending:
            pending["url"] = line
            out.append(pending)
            pending = None
    return out


def probe(ch, timeout):
    """Returns (status, detail) where status is OK or a short failure category."""
    url = ch["url"]
    scheme = url.split("://", 1)[0].lower() if "://" in url else ""
    if scheme not in PROTOCOLS:
        return "PROTOCOL", f"{scheme}:// not built in"
    if url.split("?")[0].lower().endswith(".mpd"):
        return "PROTOCOL", "DASH, no dash demuxer built in"

    cmd = [
        "ffprobe", "-v", "error", "-hide_banner",
        "-user_agent", ch["user_agent"] or UA,
        "-rw_timeout", str(timeout * 1000000),
        "-analyzeduration", "5M", "-probesize", "5M",
        "-show_entries", "stream=codec_type,codec_name,width,height:format=format_name",
        "-of", "json", "-i", url,
    ]
    if ch["referer"]:
        cmd[4:4] = ["-referer", ch["referer"]]

    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout + 10)
    except subprocess.TimeoutExpired:
        return "TIMEOUT", f"no response in {timeout}s"

    if r.returncode != 0:
        err = " ".join(r.stderr.split())
        low = err.lower()
        if "403" in err:
            return "HTTP403", err[-110:]
        if "404" in err:
            return "HTTP404", err[-110:]
        if "401" in err:
            return "HTTP401", err[-110:]
        if "5xx" in err or "server error" in low or "503" in err or "502" in err:
            return "HTTP5XX", err[-110:]
        if "timed out" in low or "timeout" in low:
            return "TIMEOUT", err[-110:]
        if "tls" in low or "ssl" in low or "certificate" in low:
            return "TLS", err[-110:]
        if "name or service" in low or "failed to resolve" in low:
            return "DNS", err[-110:]
        return "OPEN", err[-110:] or "avformat_open_input failed"

    try:
        info = json.loads(r.stdout)
    except json.JSONDecodeError:
        return "OPEN", "no stream info"

    fmt = info.get("format", {}).get("format_name", "")
    streams = info.get("streams", [])
    video = [s for s in streams if s.get("codec_type") == "video"]
    audio = [s for s in streams if s.get("codec_type") == "audio"]

    if not any(f in DEMUXERS for f in fmt.split(",")):
        return "DEMUX", f"container {fmt}"
    if not video:
        return "NOVIDEO", f"audio only ({audio[0]['codec_name'] if audio else 'nothing'})"

    v, a = video[0], (audio[0] if audio else None)
    vc = v.get("codec_name", "?")
    ac = a.get("codec_name", "?") if a else None
    if vc not in VIDEO_DECODERS:
        return "VCODEC", f"{vc} not built in"
    if ac and ac not in AUDIO_DECODERS:
        return "ACODEC", f"{ac} not built in"

    size = f"{v.get('width', '?')}x{v.get('height', '?')}"
    return "OK", f"{vc} {size}" + (f" / {ac}" if ac else " / silent")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("source", nargs="?",
                    default="https://iptv-org.github.io/iptv/countries/id.m3u")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--jobs", type=int, default=10)
    ap.add_argument("--tsv", help="write the full result table here")
    ap.add_argument("--out", help="write a playlist of only the channels that opened")
    ap.add_argument("--confirm", action="store_true",
                    help="probe the survivors a second time and keep only those that "
                         "pass twice, so a momentary hiccup does not ship a dead row")
    args = ap.parse_args()

    if "://" in args.source:
        text = urllib.request.urlopen(args.source, timeout=30).read().decode("utf-8", "replace")
    else:
        text = open(args.source, encoding="utf-8", errors="replace").read()

    channels = parse(text)
    print(f"{len(channels)} channels\n", file=sys.stderr)

    rows = [None] * len(channels)
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(probe, c, args.timeout): i for i, c in enumerate(channels)}
        done = 0
        for f in concurrent.futures.as_completed(futures):
            i = futures[f]
            status, detail = f.result()
            rows[i] = (status, detail)
            done += 1
            print(f"[{done}/{len(channels)}] {status:9} {channels[i]['name'][:44]:46} {detail[:70]}",
                  file=sys.stderr, flush=True)

    if args.confirm:
        survivors = [i for i, (s, _) in enumerate(rows) if s == "OK"]
        print(f"\nconfirming {len(survivors)}\n", file=sys.stderr)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            futures = {pool.submit(probe, channels[i], args.timeout): i for i in survivors}
            for f in concurrent.futures.as_completed(futures):
                i = futures[f]
                status, detail = f.result()
                if status != "OK":
                    rows[i] = ("FLAKY", detail)
                    print(f"  dropped {channels[i]['name'][:44]:46} {detail[:60]}",
                          file=sys.stderr, flush=True)

    if args.out:
        with open(args.out, "w", encoding="utf-8") as fh:
            fh.write("#EXTM3U\n")
            for ch, (status, _) in zip(channels, rows):
                if status != "OK":
                    continue
                attrs = f' group-title="{ch["group"]}"'
                if ch["referer"]:
                    attrs += f' http-referrer="{ch["referer"]}"'
                if ch["user_agent"]:
                    attrs += f' user-agent="{ch["user_agent"]}"'
                fh.write(f'#EXTINF:-1{attrs},{ch["name"]}\n{ch["url"]}\n')

    if args.tsv:
        with open(args.tsv, "w", encoding="utf-8") as fh:
            fh.write("status\tname\tgroup\tdetail\treferer\turl\n")
            for ch, (status, detail) in zip(channels, rows):
                fh.write(f"{status}\t{ch['name']}\t{ch['group']}\t{detail}\t"
                         f"{ch['referer']}\t{ch['url']}\n")

    tally = {}
    for status, _ in rows:
        tally[status] = tally.get(status, 0) + 1
    print("\n--- tally ---", file=sys.stderr)
    for status, n in sorted(tally.items(), key=lambda kv: -kv[1]):
        print(f"{n:4}  {status}", file=sys.stderr)


if __name__ == "__main__":
    main()
