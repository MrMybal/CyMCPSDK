"""Optional hardware test of the CUDA runtime installed by CyMCPSDK."""

import argparse
import ctypes as c
from pathlib import Path
import platform
import subprocess
import sys


def verify_device(runtime):
    def bind(name, *arguments):
        function = getattr(runtime, name)
        function.argtypes = arguments
        function.restype = c.c_int
        return function

    runtime.cudaGetErrorString.argtypes = [c.c_int]
    runtime.cudaGetErrorString.restype = c.c_char_p

    def check(function, *arguments):
        status = function(*arguments)
        if status:
            message = runtime.cudaGetErrorString(status) or b"unknown CUDA error"
            raise RuntimeError(f"{function.__name__}: {message.decode()} ({status})")

    get_version = bind("cudaRuntimeGetVersion", c.POINTER(c.c_int))
    count_devices = bind("cudaGetDeviceCount", c.POINTER(c.c_int))
    allocate = bind("cudaMalloc", c.POINTER(c.c_void_p), c.c_size_t)
    copy = bind("cudaMemcpy", c.c_void_p, c.c_void_p, c.c_size_t, c.c_int)
    free = bind("cudaFree", c.c_void_p)
    version, devices = c.c_int(), c.c_int()
    check(get_version, c.byref(version))
    check(count_devices, c.byref(devices))
    if devices.value < 1:
        raise RuntimeError("No CUDA device is available to the verified runtime")

    size = 4096
    buffer_type = c.c_ubyte * size
    source = buffer_type(*(i % 251 for i in range(size)))
    result = buffer_type()
    device_buffer = c.c_void_p()
    check(allocate, c.byref(device_buffer), size)
    try:
        host_to_device, device_to_host = 1, 2
        check(copy, device_buffer, source, size, host_to_device)
        check(copy, result, device_buffer, size, device_to_host)
        if bytes(result) != bytes(source):
            raise RuntimeError("GPU memory round trip changed the payload")
    finally:
        check(free, device_buffer)
    major, minor = version.value // 1000, (version.value % 1000) // 10
    print(f"CUDA runtime {major}.{minor} ({version.value}), devices {devices.value}, "
          f"{size}-byte memory round trip and free passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("verifier", type=Path, help="Built cymcp_cuda_tests executable")
    parser.add_argument("cache", type=Path, help="Private SDK dependency cache directory")
    args = parser.parse_args()
    if (sys.platform not in ("win32", "linux") or c.sizeof(c.c_void_p) != 8
            or platform.machine().lower() not in ("amd64", "x86_64")):
        parser.error("The built-in CUDA runtime package requires Windows or Linux x64")
    cache = args.cache.resolve()
    verified = subprocess.run(
        [str(args.verifier.resolve(strict=True)), "--verify-nvidia-download", str(cache)],
        check=True, stdout=subprocess.PIPE, encoding="utf-8",
    )
    runtime_dir = Path(verified.stdout.strip()).resolve(strict=True)
    name = "cudart64_12.dll" if sys.platform == "win32" else "libcudart.so.12"
    library = (runtime_dir / name).resolve(strict=True)
    if not library.is_relative_to(cache):
        raise RuntimeError("The verifier returned a library outside the private cache")
    loader = c.WinDLL if sys.platform == "win32" else c.CDLL
    verify_device(loader(str(library)))


if __name__ == "__main__":
    main()
