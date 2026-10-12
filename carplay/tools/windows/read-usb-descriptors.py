"""Read Apple USB descriptors via Windows hub IOCTLs, without changing USB state.

Same query mechanism as Microsoft's USBView. Does not read serial strings,
switch configurations/modes, claim interfaces, or modify drivers/registry.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import datetime
import json
from pathlib import Path
import struct
import uuid


def parse_configuration(data):
    if len(data) < 9 or data[1] != 2 or data[0] < 9:
        raise ValueError('Invalid configuration header')
    total = struct.unpack_from('<H', data, 2)[0]
    if total != len(data):
        raise ValueError('Truncated or oversized configuration')
    result = dict(value=data[5], interface_count=data[4], interfaces=[], raw_hex=data.hex())
    current = None
    offset = 0
    while offset < total:
        if offset + 2 > total:
            raise ValueError('Truncated descriptor header')
        length, kind = data[offset:offset+2]
        if length < 2 or offset + length > total:
            raise ValueError('Invalid descriptor length')
        d = data[offset:offset+length]
        if kind == 4:
            if length < 9:
                raise ValueError('Short interface descriptor')
            current = dict(number=d[2], alternate=d[3], endpoint_count=d[4],
                           **{'class': d[5], 'subclass': d[6], 'protocol': d[7]}, endpoints=[])
            current['usbmux'] = tuple(d[5:8]) == (255, 254, 2)
            current['ncm_control'] = tuple(d[5:7]) == (2, 13)
            result['interfaces'].append(current)
        elif kind == 5:
            if length < 7 or current is None:
                raise ValueError('Invalid endpoint descriptor')
            current['endpoints'].append(dict(address=d[2], attributes=d[3],
                max_packet_size=struct.unpack_from('<H', d, 4)[0], interval=d[6]))
        offset += length
    result['has_usbmux'] = any(i['usbmux'] for i in result['interfaces'])
    result['has_ncm_control'] = any(i['ncm_control'] for i in result['interfaces'])
    return result


def collect():
    kernel = C.WinDLL('kernel32', use_last_error=True)
    setup = C.WinDLL('setupapi', use_last_error=True)
    class GUID(C.Structure):
        _fields_ = [('bytes', C.c_ubyte * 16)]
    class Interface(C.Structure):
        _fields_ = [('size', W.DWORD), ('guid', GUID), ('flags', W.DWORD), ('reserved', C.c_void_p)]
    guid = GUID.from_buffer_copy(uuid.UUID('f18a0e88-c30c-11d0-8815-00a0c906bed8').bytes_le)
    setup.SetupDiGetClassDevsW.argtypes = [C.POINTER(GUID), W.LPCWSTR, W.HWND, W.DWORD]
    setup.SetupDiGetClassDevsW.restype = W.HANDLE
    setup.SetupDiEnumDeviceInterfaces.argtypes = [W.HANDLE, C.c_void_p, C.POINTER(GUID), W.DWORD, C.POINTER(Interface)]
    setup.SetupDiEnumDeviceInterfaces.restype = W.BOOL
    setup.SetupDiGetDeviceInterfaceDetailW.argtypes = [W.HANDLE, C.POINTER(Interface), C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]
    setup.SetupDiGetDeviceInterfaceDetailW.restype = W.BOOL
    setup.SetupDiDestroyDeviceInfoList.argtypes = [W.HANDLE]
    kernel.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
    kernel.CreateFileW.restype = W.HANDLE
    kernel.CloseHandle.argtypes = [W.HANDLE]
    kernel.DeviceIoControl.argtypes = [W.HANDLE, W.DWORD, C.c_void_p, W.DWORD, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p]
    kernel.DeviceIoControl.restype = W.BOOL
    invalid = C.c_void_p(-1).value
    def query(h, function, initial, size):
        buffer = C.create_string_buffer(size)
        C.memmove(buffer, initial, len(initial))
        returned = W.DWORD()
        if not kernel.DeviceIoControl(h, (0x22 << 16) | (function << 2), buffer, size,
                                      buffer, size, C.byref(returned), None):
            raise C.WinError(C.get_last_error())
        if returned.value > size:
            raise ValueError('Invalid IOCTL result size')
        return buffer.raw[:returned.value]
    def descriptor(h, port, index, size):
        request = struct.pack('<IBBHHH', port, 0x80, 6, (2 << 8) | index, 0, size)
        data = query(h, 260, request, 12 + size)
        if len(data) < 12:
            raise ValueError('Short descriptor IOCTL reply')
        return data[12:]
    report = dict(captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  read_only=True, devices=[], hub_errors=[], hubs_examined=0)
    listing = setup.SetupDiGetClassDevsW(C.byref(guid), None, None, 0x12)
    if listing == invalid:
        raise C.WinError(C.get_last_error())
    try:
        for hub_index in range(256):
            entry = Interface(); entry.size = C.sizeof(entry)
            if not setup.SetupDiEnumDeviceInterfaces(listing, None, C.byref(guid), hub_index, C.byref(entry)):
                if C.get_last_error() != 259:
                    raise C.WinError(C.get_last_error())
                break
            needed = W.DWORD()
            setup.SetupDiGetDeviceInterfaceDetailW(listing, C.byref(entry), None, 0, C.byref(needed), None)
            if not 8 <= needed.value <= 65536:
                raise ValueError('Invalid hub path buffer size')
            detail = C.create_string_buffer(needed.value)
            struct.pack_into('<I', detail, 0, 8 if C.sizeof(C.c_void_p) == 8 else 6)
            if not setup.SetupDiGetDeviceInterfaceDetailW(listing, C.byref(entry), detail, needed, None, None):
                raise C.WinError(C.get_last_error())
            path = C.wstring_at(C.addressof(detail) + 4)
            h = kernel.CreateFileW(path, 0x40000000, 3, None, 3, 0, None)
            if h == invalid:
                report['hub_errors'].append(dict(hub=hub_index, stage='open', error=C.get_last_error()))
                continue
            try:
                node = query(h, 258, b'\0'*4, 1024)
                if len(node) < 7:
                    raise ValueError('Short hub information')
                report['hubs_examined'] += 1
                for port in range(1, node[6]+1):
                    try:
                        info = query(h, 274, struct.pack('<I', port), 4096)
                        if len(info) < 35 or info[4:6] != b'\x12\x01':
                            continue
                        vendor, product = struct.unpack_from('<HH', info, 12)
                        if vendor != 0x05ac:
                            continue
                        device = dict(hub=hub_index, port=port, vendor=vendor, product=product,
                            current_configuration=info[22], configuration_count=info[21],
                            connection_status=struct.unpack_from('<I', info, 31)[0], configurations=[])
                        report['devices'].append(device)
                        for index in range(info[21]):
                            try:
                                header = descriptor(h, port, index, 9)
                                if len(header) < 9 or header[1] != 2:
                                    raise ValueError('Invalid configuration header')
                                total = struct.unpack_from('<H', header, 2)[0]
                                if total < 9:
                                    raise ValueError('Invalid total descriptor length')
                                config = parse_configuration(descriptor(h, port, index, total))
                                config['descriptor_index'] = index
                                device['configurations'].append(config)
                            except (OSError, ValueError) as error:
                                device['configurations'].append(dict(descriptor_index=index, error=str(error)))
                    except (OSError, ValueError) as error:
                        report['hub_errors'].append(dict(hub=hub_index, port=port, error=str(error)))
            except (OSError, ValueError) as error:
                report['hub_errors'].append(dict(hub=hub_index, error=str(error)))
            finally:
                kernel.CloseHandle(h)
    finally:
        setup.SetupDiDestroyDeviceInfoList(listing)
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = collect()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))
