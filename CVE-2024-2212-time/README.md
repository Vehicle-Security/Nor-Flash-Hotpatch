# nRF52840 Hotpatch PlatformIO

This project keeps the original flash hotpatch experiment logic and replaces
the fragile virtual COM path with a bidirectional `SEGGER RTT` console over the
board's on-board J-Link link.
