import os, sys, struct, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, r"Chameleon\Binaries\Win64\PenguinHotel-Win64-Shipping.exe")
BAK = EXE + ".preimport.bak"
DLL_NAME = b"chameleon_hook.dll\x00"
IMP_FUNC = b"ChameleonInit\x00"
SEC_NAME = b".chmimp\x00"


def align(v, a): return (v + a - 1) & ~(a - 1)


def parse(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    num = struct.unpack_from("<H", data, pe + 6)[0]
    size_opt = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    sec_off = opt + size_opt
    sec_align = struct.unpack_from("<I", data, opt + 32)[0]
    file_align = struct.unpack_from("<I", data, opt + 36)[0]
    size_image = struct.unpack_from("<I", data, opt + 56)[0]
    imp_rva, imp_sz = struct.unpack_from("<II", data, opt + 120)
    return dict(pe=pe, num=num, opt=opt, sec_off=sec_off, sec_align=sec_align, file_align=file_align, size_image=size_image, imp_rva=imp_rva, imp_sz=imp_sz)


def rva_to_off(data, info, rva):
    for i in range(info["num"]):
        b = info["sec_off"] + i * 40
        _, va, rsize, raddr = struct.unpack_from("<IIII", data, b + 8)
        if va <= rva < va + rsize:
            return raddr + (rva - va)
    return None


def check():
    data = open(EXE, "rb").read()
    info = parse(data)
    names = [data[info['sec_off']+i*40:info['sec_off']+i*40+8].rstrip(b'\x00') for i in range(info['num'])]
    present = SEC_NAME.rstrip(b'\x00') in names
    print(f"Sections: {info['num']}  ImportDir RVA=0x{info['imp_rva']:X} Size=0x{info['imp_sz']:X}")
    print(f"Import section {SEC_NAME.rstrip(chr(0).encode()).decode()} present: {present}")
    print(f"Backup: {'EXISTS' if os.path.exists(BAK) else 'NONE'}")


def restore():
    if not os.path.exists(BAK):
        print("No .preimport.bak"); return
    shutil.copy2(BAK, EXE)
    print("Restored exe from .preimport.bak")


def apply():
    if not os.path.exists(BAK):
        print(f"Backup -> {os.path.basename(BAK)}"); shutil.copy2(EXE, BAK)
    else:
        print("Restoring backup first (clean re-apply)"); shutil.copy2(BAK, EXE)

    data = bytearray(open(EXE, "rb").read())
    info = parse(data)

    off = rva_to_off(data, info, info["imp_rva"])
    descs = []
    while True:
        d = bytes(data[off:off+20]); off += 20
        if d == b"\x00" * 20: break
        descs.append(d)
    print(f"Existing import descriptors: {len(descs)}")

    R = align(info["size_image"], info["sec_align"])
    raw_ptr = align(len(data), info["file_align"])

    n_desc = len(descs) + 2
    arr_off = 0
    ilt_off = arr_off + n_desc * 20
    iat_off = ilt_off + 16
    iname_off = iat_off + 16
    iname_off = align(iname_off, 2)
    dllname_off = iname_off + 2 + len(IMP_FUNC)

    blob = bytearray()

    for d in descs:
        blob += d
    new_desc = struct.pack("<IIIII", R + ilt_off, 0, 0, R + dllname_off, R + iat_off)
    
    blob += new_desc
    blob += b"\x00" * 20
    blob += struct.pack("<QQ", R + iname_off, 0)
    blob += struct.pack("<QQ", R + iname_off, 0)
    blob += struct.pack("<H", 0) + IMP_FUNC
    while len(blob) < dllname_off: blob += b"\x00"
    blob += DLL_NAME

    virt_size = len(blob)
    raw_size = align(virt_size, info["file_align"])

    hdr = info["sec_off"] + info["num"] * 40
    first_raw = min(struct.unpack_from("<I", data, info['sec_off']+i*40+20)[0] for i in range(info['num']))
    if hdr + 40 > first_raw:
        print("ERROR: no header room for a new section"); return
    struct.pack_into("<8sIIII", data, hdr, SEC_NAME, virt_size, R, raw_size, raw_ptr)
    struct.pack_into("<IIHHI", data, hdr + 24, 0, 0, 0, 0, 0xC0000040)

    struct.pack_into("<H", data, info["pe"] + 6, info["num"] + 1)
    new_size_image = align(R + virt_size, info["sec_align"])
    struct.pack_into("<I", data, info["opt"] + 56, new_size_image)
    struct.pack_into("<II", data, info["opt"] + 120, R + arr_off, n_desc * 20)
    struct.pack_into("<II", data, info["opt"] + 112 + 11 * 8, 0, 0)

    if len(data) < raw_ptr: data += b"\x00" * (raw_ptr - len(data))
    data += blob + b"\x00" * (raw_size - len(blob))

    open(EXE, "wb").write(data)
    print(f"Added import of {DLL_NAME.rstrip(b_null()).decode()} !{IMP_FUNC.rstrip(b_null()).decode()}")
    print(f"  Section {SEC_NAME.rstrip(b_null()).decode()} @ RVA 0x{R:X} raw 0x{raw_ptr:X} ({virt_size} bytes)")
    print(f"  Import dir -> RVA 0x{R:X} ({n_desc} descriptors)")
    print("  Place chameleon_hook.dll next to the exe and just launch the game.")


def b_null(): return b"\x00"


def main():
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "--check": check()
    elif arg == "--restore": restore()
    else: apply()


if __name__ == "__main__":
    main()
