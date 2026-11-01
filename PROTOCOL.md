# EWEADN G5 vendor protocol notes

Everything here was reverse engineered from usbhid-dump captures of
a live G5 mouse and from the vendor's web driver bundle
(hub.eweadn.cn, channel `yjs`). It documents interface 2, the vendor
command channel, and the input report on interface 0 that g5moused
decodes. Use at your own risk; writing a wrong frame can reset the
button mapping.

## Devices

| USB ID      | What it is                                          |
|-------------|-----------------------------------------------------|
| a8a5:2255   | original 2.4 GHz receiver                           |
| a8a4:2255   | replacement receiver shipped from October 2026, and the mouse itself when connected by USB cable |

Both use the same interface layout:

* interface 0: mouse data, interrupt IN endpoint 0x81, vendor report
  (see g5moused.c for the 9-byte frame)
* interface 1: standard keyboard, works out of the box
* interface 2: vendor command channel, two endpoints

## Command channel (interface 2)

Frames are 65-byte reports on the two interrupt endpoints (one IN,
one OUT). Read frame shape:

    00 55 <cmd> A5 0B <param> 01 01 01 00 00 ...

Replies start with `AA <cmd> A5`, heartbeat pushes with
`AA FA AE` (carry dpi and report rate state). Write frame
(setMouseConfigData):

    01=55  02=0F  03=AE  04=0A  05=30
    + report rate at offset 11 (level + 1)
    + dpi table at offsets 14..25
    + highspeed_mode flag at offset 54

Known commands:

| cmd | meaning                       |
|-----|-------------------------------|
| 48  | battery query, level in reply byte 8 |
| 14  | read configuration            |
| 8   | read button table             |
| 9   | reset button table            |
| 13  | macro read                    |
| 16  | macro write                   |

## Button notes

The firmware may map a side button onto the left button bit by
default. That is a configuration layer thing and can be changed
with the button table commands above, not a g5moused limitation.

## Related work

* EWEADN H2 reference implementation (33-byte older protocol, same
  family, different generation):
  github.com/MuhtasimJawad/EWEADN-H2-Control
* An ALWAYS_POLL quirk for the original receiver VID (a8a5) is
  merged into the kernel for Linux 7.4; the a8a4 VID is pending
  upstream.
