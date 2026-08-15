# SACD flag tool

Toggles the SACD flag on the PS3 BD drive over ATAPI

Confirmed working on BMD-006 and older drives (`PS-SYSTEM 302R`). Needs a CFW with lv2 peek/poke available.

**Run it on CEX** DEX firmware has no SACD support built in, so the flag can be set from DEX and read back correctly while the disc still comes up as its CD layer. Same drive, same flag, same tool, swap to CEX and the disc is presented.

There is no firmware-specific offset to look up: the access check is found by scanning lv2 for its instruction sequence.

## Menu

- **Scan lv2 for the access check** - finds the function on whatever firmware is running. About a second, with a progress bar.
- **Read drive state** - patch, open, inquiry, profile, D7 read, restore. Writes nothing to the drive.
- **Enable SACD (0xff)** / **Disable SACD (0x53)** - the same sequence with the D7 write in the middle and the readback checked afterwards. `0xff` is the value that gets the SACD layer presented; `0x53` is the drive's normal state. **Reboot the console afterwards** - the flag persists in the drive's config flash, but the drive will not re-read a disc it has already identified.
- **lv2 patch: ON / OFF** - OFF runs the drive sequence without touching the kernel at all, for when the drive is reachable without the patch.
  
## Building

Needs the official SCEI SDK, scetool and ps3py. None of them are vendored here, point `CELL_SDK` at the SDK, put `scetool.exe` with its `data/` keys in `scetool/`, and ps3py in `ps3py/`.

```
python build.py            # compile + sign self
python build.py elf        # compile only, for OpenTM
python build.py fself      # fake signed self
python make_pkg.py         # retail npdrm .pkg
```


## Notes

The ATAPI `0xfd/0x11` profile read is not supported by every drive; on a 302R it returns `0xffffffff` and the `0x20000` "declaring SACD" confirmation is simply unavailable. The D7 readback is the evidence that the flag took.

The flag persists in the drive's config flash across power cycles, so a run that reports `flag is already 0xff` is not a failure, it means an earlier run or other tool set it.