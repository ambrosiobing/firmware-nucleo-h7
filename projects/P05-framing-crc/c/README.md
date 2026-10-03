# P05 in C

**State: host only, and proven there.** `frame.c` is the frame, `cobs.c` the byte
stuffing comparison, `crc16.c` the software CRC.

Both implementations reach the published check value 0x29B1 and every single-bit
corruption is rejected. The peripheral half, which is the hardware CRC unit
configured to agree with this software, waits on RM0455.


## What it is proven against

The published check value 0x29B1 and the rejection of every single-bit corruption.
