#!/usr/bin/env python3
"""Turns Natural Earth 110m admin-0 into the compact world.bin the globe reads.

    curl -sLO https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_countries.geojson
    python3 tools/build-world.py ne_110m_admin_0_countries.geojson romfs/world.bin

Coordinates are stored as degrees times 100 in int16, which is about a kilometre
of precision -- an order of magnitude finer than one pixel at this globe size.
Interior rings are dropped: a lake is not something you can tune a radio to.
"""

import json
import struct
import sys

MAGIC = b"NXGL"
SCALE = 100


def rings_of(geometry):
    if geometry["type"] == "MultiPolygon":
        polygons = geometry["coordinates"]
    else:
        polygons = [geometry["coordinates"]]
    return [polygon[0] for polygon in polygons if polygon]


def main(src, dst):
    with open(src) as handle:
        data = json.load(handle)

    countries = []
    for feature in data["features"]:
        props = feature["properties"]
        iso = props.get("ISO_A2_EH") or props.get("ISO_A2") or ""
        if len(iso) != 2:
            # Radio Browser keys stations by ISO 3166-1 alpha-2, so a country we
            # cannot key is a country we could never look up stations for.
            continue

        name = props.get("NAME") or iso
        rings = rings_of(feature["geometry"])
        if rings:
            countries.append((iso, name, rings))

    out = bytearray(MAGIC)
    out += struct.pack("<H", len(countries))

    for iso, name, rings in countries:
        encoded = name.encode("utf-8")[:63]
        out += iso.encode("ascii")
        out += struct.pack("<B", len(encoded)) + encoded
        out += struct.pack("<H", len(rings))
        for ring in rings:
            out += struct.pack("<H", len(ring))
            for lon, lat in ring:
                out += struct.pack("<hh", round(lon * SCALE), round(lat * SCALE))

    with open(dst, "wb") as handle:
        handle.write(out)

    print(f"{len(countries)} negara, {len(out)} byte -> {dst}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
