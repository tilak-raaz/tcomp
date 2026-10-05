# The .tcmp file format

This document specifies every byte of a `.tcmp` file. It should be precise enough to write a compatible decoder without reading tcomp's source code.

Conventions: offsets and sizes are in bytes. Multi-byte integers will be little-endian (none exist yet in version 0).

## Version 0 (current, M0)

Version 0 is a minimal container used while the real codec is built. It exists so the CLI, tests and CI can be exercised end to end.

| Offset | Size | Field | Value |
| --- | --- | --- | --- |
| 0 | 4 | Magic | ASCII `TCMP` (`54 43 4D 50`) |
| 4 | 1 | Format version | `0` |
| 5 | 1 | Method | `0` = STORE |
| 6 | rest of file | Payload | Depends on method |

### Methods

| Id | Name | Payload |
| --- | --- | --- |
| 0 | STORE | The original data, unchanged, up to end of file |

All other method ids are reserved.

### Decoder rules

A decoder must check, in this order:

1. The bytes present match the magic. Fewer than 4 bytes that do not match the start of `TCMP` → *bad magic*.
2. At least 6 header bytes exist → otherwise *truncated*.
3. Format version ≤ the highest version the decoder supports → otherwise *unsupported version*.
4. Method id is known → otherwise *unknown method*.

### Known limitations of version 0

- No stored length and no checksum, so a STORE file truncated inside its payload cannot be detected. Version 1 (M5) adds the original size and a CRC32.

## Planned changes

| Version | Milestone | Adds |
| --- | --- | --- |
| 0 + method 1 | M2 | Huffman method with canonical code lengths |
| 1 | M5 | Original size, CRC32, LZ77+Huffman method, `info` command |
| 1 + blocks | M7 | Block structure for streaming and parallelism |
