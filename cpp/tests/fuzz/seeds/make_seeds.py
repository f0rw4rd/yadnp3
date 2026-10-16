"""Writes the seed inputs of the session fuzzers, one directory per fuzzer, next to this script.

The session fuzzers take a setup byte, then requests or responses as [flags][length][bytes]; fuzzdecoder_logging takes
a byte choosing the layer (0 link, 1 transport, 2 application), then the bytes to decode.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def chunk(flags, hexstr):
    b = bytes.fromhex(hexstr)
    return bytes([flags, len(b)]) + b


def write(target, name, data):
    d = os.path.join(HERE, target)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, name), 'wb') as f:
        f.write(data)


REQUESTS = [
    'C0 01 3C 01 06',  # class 0
    'C0 01 3C 02 06 3C 03 06 3C 04 06',  # class 1, 2 and 3
    'C0 19 46 03 5B 01 1D 00 1A 00 03 00 00 00 00 00 00 00 FF 01 00 00 00 00 00 00 00 00 01 00 00 08 00 00 61 62 63',
    'C1 01 46 05 5B 01 08 00 07 00 00 00 00 00 00 00',  # file block read
    'C2 1A 46 04 5B 01 0D 00 07 00 00 00 00 00 00 00 00 08 00 00 00',  # close
    'C0 1D 46 02 5B 01 10 00 0C 00 02 00 0E 00 02 00 00 00 00 00 75 31 70 31',  # authenticate
    'C0 1C 46 07 5B 01 15 00 14 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 78',  # file info
    'C0 02 46 05 5B 01 0C 00 07 00 00 00 00 00 00 80 AA BB CC DD',  # file block write
    'C0 16 3C 04 06 01 00 00 01 02',  # assign class
    'C0 03 0C 01 28 01 00 01 00 01 01 64 00 00 00 64 00 00 00 00',  # select
    'C0 05 29 01 17 01 02 0A 00 00 00 00',  # direct operate
    'C0 14 3C 02 06',  # enable unsolicited
    'C0 07 14 00 06',  # freeze
    'C0 02 32 01 07 01 D2 04 00 00 00 00',  # write time
    'C0 18',  # record current time
    'C0 0D',  # cold restart
    'C0 01 6E 00 06',  # octet strings
    'C0 01 00 FE 06',  # device attributes
]
for i, r in enumerate(REQUESTS):
    data = bytes([0x01]) + chunk(0, r) + chunk(0x02, 'C0 00') + chunk(0, r.replace('C0', 'C1', 1))
    write('fuzzoutstation_session', f'seed{i}', data)

RESPONSES = [
    'C0 81 00 00 46 04 5B 01 0D 00 07 00 00 00 10 00 00 00 00 08 00 00 00',
    'C0 81 00 00 46 05 5B 01 0C 00 07 00 00 00 00 00 00 80 AA BB CC DD',
    'C0 81 00 00 46 06 5B 01 09 00 07 00 00 00 00 00 00 00 00',
    'C0 81 00 00 46 07 5B 01 15 00 14 00 01 00 01 00 05 00 00 00 00 00 00 00 00 00 00 00 00 00 78',
    'C0 81 00 00 46 02 5B 01 0C 00 0C 00 00 00 0C 00 00 00 78 56 34 12',
    'C0 81 00 00 0C 01 28 01 00 01 00 03 01 64 00 00 00 64 00 00 00 00',
    'E0 81 00 00 01 02 00 00 03 01 01 01 81 02 01 01 00 00 20 01 28 01 00 01 00 01 00 00 00',
    'C0 81 00 00 00 FE 5B 01 05 00 F7 03 01 61 62',
    'F0 82 80 00 02 02 28 01 00 01 00 01 00 00 00 00 00 00',
    'C0 81 00 00 34 02 07 01 05 00',
    'C0 81 00 00 6E 03 00 00 00 61 62 63',
    'C0 81 00 00 33 01 07 01 00 00 00 00 00 00 02 01 28 01 00 01 00 01 00 00',
]
for i, r in enumerate(RESPONSES):
    for setup in range(9):  # the task the master starts
        write('fuzzmaster_session', f'seed{i}_{setup}', bytes([setup]) + chunk(0x01, r) + chunk(0x01, r))

APDUS = RESPONSES[:5] + REQUESTS[:5] + [
    'C0 20 78 01 5B 01 10 00 01 00 00 00 01 00 04 01 04 00 AA BB CC DD',
    'C0 02 22 01 28 01 00 01 00 05 00',
]
for i, a in enumerate(APDUS):
    b = bytes.fromhex(a)
    write('fuzzdecoder_logging', f'apdu{i}', bytes([2]) + b)
    write('fuzzdecoder_logging', f'tpdu{i}', bytes([1, 0xC0]) + b)
write('fuzzdecoder_logging', 'lpdu0', bytes([0]) + bytes.fromhex('05 64 0B C4 00 04 01 00 CA 8A C0 C0 01 3C 01 06 FF 50'))
