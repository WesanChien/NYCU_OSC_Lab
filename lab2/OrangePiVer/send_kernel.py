import struct
import sys
import time

PORT = "/dev/ttyUSB0"

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 send_kernel.py loader_target.bin [port]")
        return

    image_path = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) >= 3 else PORT

    with open(image_path, "rb") as f:
        data = f.read()

    header = struct.pack("<II", 0x544F4F42, len(data)) # < 就是 little-endian, II 是兩個 unsigned int

    print(f"Sending {len(data)} bytes to {port}...")

    with open(port, "wb", buffering=0) as tty:
        tty.write(header)
        tty.write(data)

    print("Done.")

if __name__ == "__main__":
    main()