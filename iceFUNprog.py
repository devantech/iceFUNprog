import argparse
from enum import IntEnum
import re
import sys
try:
    import serial
except ImportError:
    print("pyserial not installed, install it using: pip install pyserial")
    sys.exit(1)

class Commands(IntEnum):
    DONE         = 0xb0
    GET_VER      = 0xb1
    RESET_FPGA   = 0xb2
    ERASE_CHIP   = 0xb3
    ERASE_64k    = 0xb4
    PROG_PAGE    = 0xb5
    READ_PAGE    = 0xb6
    VERIFY_PAGE  = 0xb7
    GET_CDONE    = 0xb8
    RELEASE_FPGA = 0xb9

FLASH_SIZE = 0x100000 # 1MByte because that is the size of the Flash chip

class ProgramError(Exception):
    def __init__(self, address: int, expected: int, read: int):
        self.address = address
        self.expected = expected
        self.read = read

    def __str__(self) -> str:
        return f"Program failed at 0x{self.address:06x}, 0x{self.expected:02x} expected, 0x{self.read} read"

class VerifyError(ProgramError):
    def __str__(self) -> str:
        return f"Verify failed at 0x{self.address:06x}, 0x{self.expected:02x} expected, 0x{self.read} read"

class IceFunProg:
    def __init__(self, port: str | None = None):
        self.ser = serial.Serial(port, baudrate=19200, timeout=1)

    def _read(self, length: int) -> bytes:
        data = self.ser.read(length)
        if len(data) < length:
            raise TimeoutError("Timed out waiting for serial port")
        return data

    def get_version(self) -> int | None:
        self.ser.write(bytes([Commands.GET_VER]))
        try:
            data = self._read(2)
        except TimeoutError:
            return None

        if data[0] == 38:
            return data[1]

    def reset_fpga(self) -> tuple[int, int, int]:
        self.ser.write(bytes([Commands.RESET_FPGA]))
        data = self._read(3)
        return tuple(data)

    def release_fpga(self):
        self.ser.write(bytes([Commands.RELEASE_FPGA]))
        self._read(1)

    def erase_page(self, page: int):
        self.ser.write(bytes([Commands.ERASE_64k, page]))
        self._read(1)

    def program_page(self, address: int, data: bytes):
        if address < 0:
            raise ValueError("Address cannot be negative")
        if address >> 24:
            raise ValueError("Address too large")
        b = bytearray([
            Commands.PROG_PAGE,
            address >> 16,
            (address >> 8) & 0xff,
            address & 0xff
        ])
        b += data + bytes(256 - len(data)) # pad out to 256 bytes
        self.ser.write(b)
        r = self._read(4)
        if r[0] != 0:
            raise ProgramError(
                address=address - 256 + r[1] - 4,
                expected=r[2],
                read=r[3]
            )

    def verify_page(self, address: int, data: bytes):
        if address < 0:
            raise ValueError("Address cannot be negative")
        if address >> 24:
            raise ValueError("Address too large")
        b = bytearray([
            Commands.VERIFY_PAGE,
            address >> 16,
            (address >> 8) & 0xff,
            address & 0xff
        ])
        b += data + bytes(256 - len(data)) # pad out to 256 bytes
        self.ser.write(b)
        r = self._read(4)
        if r[0] != 0:
            raise VerifyError(
                address=address - 256 + r[1] - 4,
                expected=r[2],
                read=r[3]
            )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument("--port", "-P", type=str, help="The serial port to connect to, e.g. COM12, /dev/ttyUSB0")
    parser.add_argument("--offset", "-o", type=str, default="0", help="""Start address for write [default: 0]
(append 'k' to the argument for size in kilobytes, or 'M' for size in megabytes)
or prefix '0x' to the argument for size in hexdecimal
""")
    parser.add_argument("--skip-verification", "-v", action="store_true", help="Skip Verification")
    parser.add_argument("bitstream_file", help="The bitstream file to write (.bin)")

    args = parser.parse_args()

    rw_offset = 0
    if args.offset is not None:
        m = re.match(r"^([0-9]+|0x[0-9a-fA-F]+)([kM]?)$", args.offset)
        if not m:
            print(f"Invalid offset: {args.offset!r}")
            sys.exit(1)
        rw_offset = int(m.group(1), base=0) # accept dec or hex
        if m.group(2) == "k":
            rw_offset *= 1024
        elif m.group(2) == "M":
            rw_offset *= 1024*1024

    try:
        with open(args.bitstream_file, "rb") as f:
            bitstream = f.read()
    except Exception as ex:
        print(f"Error opening bitstream: {ex}")
        sys.exit(1)

    if len(bitstream) > FLASH_SIZE:
        print(f"Bitstream larger than flash size: {len(bitstream)} > {FLASH_SIZE}")
        sys.exit(1)

    try:
        p = IceFunProg(args.port)
    except Exception as ex:
        print(f"Failed to open serial port: {ex}")
        sys.exit(1)

    version = p.get_version()
    if version is None:
        print("Error getting Version")
        sys.exit(1)

    flash_id = p.reset_fpga()

    print(f"iceFUN v{version}, Flash ID 0x{flash_id[0]:02x} 0x{flash_id[1]:02x} 0x{flash_id[2]:02x}")

    start_page = rw_offset >> 16
    end_page = start_page + (len(bitstream) >> 16) + 1
    for page in range(start_page, end_page):
        print(f"Erasing sector 0x{page << 16:06x}")
        p.erase_page(page)

    print(f"File Size: {len(bitstream)}")

    try:
        i = 0
        print("Programming ", end="", flush=True)
        for address in range(0, len(bitstream), 256):
            p.program_page(rw_offset+address, bitstream[address:address+256])
            i += 1
            if i % 10 == 0:
                print(".", end="", flush=True)
        print()
    except ProgramError as ex:
        print(ex)
        sys.exit(1)

    if not args.skip_verification:
        try:
            i = 0
            print("Verifying ", end="", flush=True)
            for address in range(0, len(bitstream), 256):
                p.verify_page(rw_offset+address, bitstream[address:address+256])
                i += 1
                if i % 10 == 0:
                    print(".", end="", flush=True)
            print()
        except VerifyError as ex:
            print(ex)
            sys.exit(1)

    p.release_fpga()
