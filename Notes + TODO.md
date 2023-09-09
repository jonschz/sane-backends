# Notes + TODO

## TODO
- Code still doesn't work after interface fix
- myexample.script: Use the non-SANE tool instead, see if it works

- For some really inconvenient reason, libusb claims the interface in `sanei_open_usb()`
  - does it even know which interface is the right one to claim?
    - some heuristic; sees that interface 0 doesn't appear to be a scanner, chooses 1 instead
- Obvious solution: Go for manual libusb; we need libusb anyway for control signals
  - Easier fix: Close interface immediately; not sure if this fixes the issue, though
- This might explain the interruptions, though I am not sure
  - other possible explanation: Timeout value in scanbd


- Test newly added `cancel()` support
- look into kvs driver + multi page handling -> important for scanbd
  - can we trigger multi page without scanbd for testing? I don't think so
  - Need another recording with 4 pages to correctly analyze the "end of page" packet
  - it looks like you just keep returning data / lines with status GOOD; at some point, you return EOF
    - scanimage should be able to deal with that: see https://askubuntu.com/a/383573
    - use option ` --batch="/path/to/scanned_image-%d.tiff" `
    - may have to use options `-x` and `-y`; comment says that all images may end up on the same page otherwise
      - this may be because the scanner returns -1 for the page length; I have to try
- Scan does not finish
  - Button + normal scanimage call: no issue
  - 
- go over list of supported devices on brscan4, consider adding all of these to "untested"
    - what about auto-detection?
- Under which conditions does one list a backend under all libs but not under convenience_libs?
  - What is the `-s.c` file / nodist good for?
  - can leave it out
- Test grayscale NONE vs JPEG gamma, reuse gamma curve analysis from RGB
  - Improve plot tool by introducing some blur with low diameter (3x3, max. 5x5)
    - use numpy convolution
  - try: raw return values into known color spaces (CIE, LUV)


## sanebd Knowhow
- Apparently, the sanebd functionality can be implemented into sane directly via options; no need to write a scanbuttond backend
- Not well documented, unfortunately
- Default interface appears to work as follows:
  - We have some read-only option parameters named e.g. `SANE_NAME_EMAIL` (click there for more details)
  - See `hp5400_sane.c` for an example
- State of buttons is read from the device when the option parameters are read
- return a specific value when a button has been pressed
- see also `scanbd.conf` in _scanbd_ for details on all possible parameters
- **Partial success**: option "email", type string, value "email" is recognized!

- What works:
  - Triggering via "email"
  - running the script from /usr/local/etc/saned
  - Trigger only when button is pressed
  - set environment variable to select the mode
- What needs to be done:
  - debug segfaults / double free issue
    - run again with `-fsanitize=address` enabled for both
    - `CFLAGS="-g -O -Wall -fsanitize=address" ./configure --disable-shared`
    - skip memory leak detection during compile with `ASAN_OPTIONS="detect_leaks=0"`
  - see if we can make sense of the "scan" property; what is going on with the values "1" vs ""?

- Found bug:
  - if SCAN triggers and GLOBALTEST is enabled, SCANBD_FUNCTION is set to (null) incorrectly. Maybe this only happens if the values are set right at startup? Not sure
  - possible solution in scanbd code (sane.c)

## Official checklist:
* To add the backend to the existing SANE code, the following must be done at
  least:
    [ ] add the backend name to ALL_BACKENDS in configure.in (and run autoreconf)
    - Add new backend to 
        - [ ] BACKEND_CONFS (prob. autoconfig.in, need proper git repository),
        - [ ] be_convenience_libs (?),
        - [x] be_dlopen_libs,
      and define 
        - [x] _lib${backend}_la_SOURCES and 
        - [x] nodist_libsane_${backend}_la_SOURCES;
      using an existing backend as
      a template.  Any sanei reference code should be listed in
      libsane_${backend}_la_LIBADD as well as any external libraries
      required to resolve all symbols.
    [x] Add the source code files to the backend/ directories. All file names 
      must start with the backend name (e.g. newbackend.c, newbackend.h and
      newbackend-usb.c). 
- add remaining entries here like autoformat to GNU (at the end)


## Notes on files
- `newbackend.conf.in` not needed because of DLL
