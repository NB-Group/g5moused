# g5moused

Userspace driver for EWEADN G5 wireless mice (YJX-CHIP receivers,
USB IDs a8a5:2255 and a8a4:2255) on Linux.

These mice send motion and button data in a vendor-defined report
that the kernel HID stack does not map, so the pointer is dead out
of the box: no cursor, no clicks, while the same mouse works on
Windows without any install. The vendor only ships a Windows driver
and a web configuration tool.

This daemon claims the mouse interface with libusb, parses the
vendor report and replays it through uinput. It relays every device
that is present at startup, so the original receiver, the October
2026 replacement receiver and the mouse itself on its USB cable all
work, alone or plugged in together.

## Report format

9 bytes on endpoint 0x81 of interface 0, sent at up to 1 kHz:

    [0]  0x02    report id
    [1]  buttons bit0 left, bit1 right, bit2 middle, bit3 back,
                   bit4 forward
    [2]  unused
    [3]  dx, int16 little-endian
    [5]  dy, int16 little-endian
    [7]  wheel, int8
    [8]  unused

The surrounding vendor command protocol (DPI, report rate, battery
query, button remaps) is documented in [PROTOCOL.md](PROTOCOL.md).

## Build and install

    make
    sudo make install

The install target puts the binary in /usr/local/bin and a systemd
unit in /etc/systemd/system, then you enable it once:

    sudo systemctl enable --now g5moused

Requires libusb 1.0 and root to detach the kernel HID driver from
the interface.

## Kernel side

An ALWAYS_POLL quirk for the original receiver (a8a5) is merged into
the kernel for Linux 7.4. It fixes a separate problem: on boot or
replug the pointer stays dead until something opens the input node.
It does not decode the vendor report, which is why this daemon
exists. A proper in-kernel HID driver is being worked on and will
make g5moused unnecessary.

## License

GPL-2.0, see [LICENSE](LICENSE).
