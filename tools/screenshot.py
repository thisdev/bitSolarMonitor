#!/usr/bin/env python3
"""Holt Bildschirmaufnahmen vom laufenden bitSolarMonitor und legt sie als
PNG ab. Setzt voraus, dass CONFIG_PVMON_SHOT_ENABLE gesetzt ist.

    python3 tools/screenshot.py 192.168.1.76              # aktuelle Seite
    python3 tools/screenshot.py 192.168.1.76 --alle       # alle drei Seiten
    python3 tools/screenshot.py 192.168.1.76 --seite 2

PNG wird ohne Fremdbibliothek geschrieben, es braucht nur zlib aus der
Standardbibliothek.
"""
import argparse, struct, sys, time, urllib.request, zlib
from pathlib import Path

W, H = 368, 448


def hole(host: str, pfad: str, timeout: float = 20.0) -> bytes:
    with urllib.request.urlopen(f"http://{host}{pfad}", timeout=timeout) as r:
        return r.read()


def rgb565_zu_rgb888(roh: bytes, w: int, h: int) -> bytes:
    """Wandelt in Scanlines mit vorangestelltem Filterbyte, wie PNG sie will."""
    aus = bytearray()
    for y in range(h):
        aus.append(0)                                  # Filter: keiner
        zeile = roh[y * w * 2:(y + 1) * w * 2]
        for x in range(0, len(zeile), 2):
            v = zeile[x] | (zeile[x + 1] << 8)         # little endian
            r = (v >> 11) & 0x1F
            g = (v >> 5) & 0x3F
            b = v & 0x1F
            # 5 bzw. 6 Bit auf volle 8 Bit strecken, damit Weiss auch weiss wird
            aus += bytes(((r * 255 + 15) // 31,
                          (g * 255 + 31) // 63,
                          (b * 255 + 15) // 31))
    return bytes(aus)


def schreibe_png(pfad: Path, rgb: bytes, w: int, h: int) -> None:
    def chunk(typ: bytes, daten: bytes) -> bytes:
        return (struct.pack(">I", len(daten)) + typ + daten
                + struct.pack(">I", zlib.crc32(typ + daten) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)   # 8 Bit, Truecolor
    pfad.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", ihdr)
                     + chunk(b"IDAT", zlib.compress(rgb, 9))
                     + chunk(b"IEND", b""))


def aufnehmen(host: str, ziel: Path) -> None:
    roh = hole(host, "/shot")
    erwartet = W * H * 2
    if len(roh) != erwartet:
        sys.exit(f"Unerwartete Datenmenge: {len(roh)} statt {erwartet} Bytes")
    schreibe_png(ziel, rgb565_zu_rgb888(roh, W, H), W, H)
    print(f"{ziel}  ({ziel.stat().st_size // 1024} KB)")


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument("host", help="IP des Monitors, etwa 192.168.1.76")
    p.add_argument("--seite", type=int, help="Seite 0 bis 2 vorher einstellen")
    p.add_argument("--alle", action="store_true", help="alle drei Seiten nacheinander")
    p.add_argument("--demo", action="store_true",
                   help="Vorfuehrbetrieb: runde Beispielwerte, keine echten Netzdaten")
    p.add_argument("--ordner", default="docs/images", help="Zielordner")
    a = p.parse_args()

    ordner = Path(a.ordner)
    ordner.mkdir(parents=True, exist_ok=True)
    namen = ["seite1-erzeugung", "seite2-tagesverlauf", "seite3-status"]

    if a.demo:
        hole(a.host, "/demo?on=1")
        time.sleep(1.5)

    # Nach jedem Umschalten bewusst Zeit lassen. Eine Aufnahme mit halb
    # aufgebauter Seite muss man ohnehin wiederholen.
    RUHE = 1.5

    if a.alle:
        for n in range(3):
            hole(a.host, f"/page?n={n}")
            time.sleep(RUHE)
            aufnehmen(a.host, ordner / f"{namen[n]}.png")

        if a.demo:
            # Zusaetzlich der Abendzustand: Sonne weg, Speicher traegt das Haus
            hole(a.host, "/demo?on=2")
            time.sleep(RUHE)
            hole(a.host, "/page?n=0")
            time.sleep(RUHE)
            aufnehmen(a.host, ordner / "seite1-speicher-abend.png")
    else:
        if a.seite is not None:
            hole(a.host, f"/page?n={a.seite}")
            time.sleep(RUHE)
        ziel = ordner / (f"{namen[a.seite]}.png" if a.seite is not None else "aufnahme.png")
        aufnehmen(a.host, ziel)


    if a.demo:
        # Wieder auf echte Werte zurueckschalten
        hole(a.host, "/demo?on=0")


if __name__ == "__main__":
    main()
