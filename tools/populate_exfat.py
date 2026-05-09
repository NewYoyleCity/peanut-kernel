#!/usr/bin/env python3
import argparse
import math
import struct

SECTOR = 512
EOC = 0xFFFFFFFF


def le16(v):
    return struct.pack("<H", v)


def le32(v):
    return struct.pack("<I", v)


def le64(v):
    return struct.pack("<Q", v)


def read_u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def write_u32(buf, off, value):
    struct.pack_into("<I", buf, off, value)


def cluster_lba(heap_lba, spc, cluster):
    return heap_lba + (cluster - 2) * spc


def cluster_off(heap_lba, spc, cluster):
    return cluster_lba(heap_lba, spc, cluster) * SECTOR


def find_free_clusters(image, fat_off, cluster_count, count, reserved):
    found = []
    for cluster in range(2, cluster_count + 2):
        if cluster in reserved:
            continue
        if read_u32(image, fat_off + cluster * 4) == 0:
            found.append(cluster)
            if len(found) == count:
                return found
    raise RuntimeError("not enough free exFAT clusters")


def set_chain(image, fat_off, clusters):
    for i, cluster in enumerate(clusters):
        nxt = EOC if i + 1 == len(clusters) else clusters[i + 1]
        write_u32(image, fat_off + cluster * 4, nxt)


def utf16_name(name):
    raw = name.encode("utf-16le")
    chars = [raw[i] | (raw[i + 1] << 8) for i in range(0, len(raw), 2)]
    return chars


def entry_set(name, attrs, first_cluster, size):
    chars = utf16_name(name)
    file_entry = bytearray(32)
    stream_entry = bytearray(32)
    name_entry = bytearray(32)

    file_entry[0] = 0x85
    file_entry[1] = 2
    file_entry[4:6] = le16(attrs)

    stream_entry[0] = 0xC0
    stream_entry[1] = 0x03
    stream_entry[3] = len(chars)
    stream_entry[20:24] = le32(first_cluster)
    stream_entry[24:32] = le64(size)

    name_entry[0] = 0xC1
    for idx, ch in enumerate(chars[:15]):
        name_entry[2 + idx * 2:4 + idx * 2] = le16(ch)

    return bytes(file_entry + stream_entry + name_entry)


def append_entries(image, root_off, cluster_bytes, entries):
    off = 0
    while off + len(entries) <= cluster_bytes:
        if image[root_off + off] == 0:
            image[root_off + off:root_off + off + len(entries)] = entries
            if off + len(entries) < cluster_bytes:
                image[root_off + off + len(entries)] = 0
            return
        if image[root_off + off] == 0x85:
            off += 32 * (image[root_off + off + 1] + 1)
        else:
            off += 32
    raise RuntimeError("no free root directory slots")


def write_file(image, fat_off, heap_lba, spc, cluster_count, reserved, data):
    cluster_bytes = spc * SECTOR
    clusters_needed = max(1, math.ceil(len(data) / cluster_bytes))
    clusters = find_free_clusters(image, fat_off, cluster_count, clusters_needed, reserved)
    reserved.update(clusters)
    set_chain(image, fat_off, clusters)
    for idx, cluster in enumerate(clusters):
        off = cluster_off(heap_lba, spc, cluster)
        start = idx * cluster_bytes
        chunk = data[start:start + cluster_bytes]
        image[off:off + cluster_bytes] = chunk + b"\x00" * (cluster_bytes - len(chunk))
    return clusters[0], len(data)


def write_dir(image, fat_off, heap_lba, spc, cluster_count, reserved):
    cluster = find_free_clusters(image, fat_off, cluster_count, 1, reserved)[0]
    reserved.add(cluster)
    set_chain(image, fat_off, [cluster])
    off = cluster_off(heap_lba, spc, cluster)
    image[off:off + spc * SECTOR] = b"\x00" * (spc * SECTOR)
    return cluster


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image")
    parser.add_argument("init")
    args = parser.parse_args()

    with open(args.init, "rb") as f:
        init_data = f.read()

    with open(args.image, "r+b") as f:
        image = bytearray(f.read())

        boot = image[:SECTOR]
        if boot[3:11] != b"EXFAT   " or boot[510:512] != b"\x55\xaa":
            raise RuntimeError("not an exFAT image")

        fat_lba = read_u32(boot, 80)
        heap_lba = read_u32(boot, 88)
        cluster_count = read_u32(boot, 92)
        root_cluster = read_u32(boot, 96)
        bps_shift = boot[108]
        spc_shift = boot[109]
        if bps_shift != 9:
            raise RuntimeError("unsupported exFAT sector size")

        spc = 1 << spc_shift
        cluster_bytes = spc * SECTOR
        fat_off = fat_lba * SECTOR
        root_off = cluster_off(heap_lba, spc, root_cluster)

        reserved = {root_cluster}
        sbin_cluster = write_dir(image, fat_off, heap_lba, spc, cluster_count, reserved)
        etc_cluster = write_dir(image, fat_off, heap_lba, spc, cluster_count, reserved)
        dev_cluster = write_dir(image, fat_off, heap_lba, spc, cluster_count, reserved)
        init_first, init_size = write_file(image, fat_off, heap_lba, spc, cluster_count, reserved, init_data)
        passwd = b"root:x:0:0:root:/root:/sbin/init\nuser:x:1000:1000:Peanut User:/home/user:/bin/sh\n"
        fstab = b"devtmpfs /dev devtmpfs rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro 0 0\n"
        passwd_first, passwd_size = write_file(image, fat_off, heap_lba, spc, cluster_count, reserved, passwd)
        fstab_first, fstab_size = write_file(image, fat_off, heap_lba, spc, cluster_count, reserved, fstab)

        sbin_off = cluster_off(heap_lba, spc, sbin_cluster)
        image[sbin_off:sbin_off + 96] = entry_set("INIT", 0x20, init_first, init_size)
        etc_off = cluster_off(heap_lba, spc, etc_cluster)
        image[etc_off:etc_off + 96] = entry_set("PASSWD", 0x20, passwd_first, passwd_size)
        image[etc_off + 96:etc_off + 192] = entry_set("FSTAB", 0x20, fstab_first, fstab_size)

        for name in ("SBIN", "BOOT", "BIN", "USR", "LIB", "ETC", "DEV"):
            first = 0
            if name == "SBIN":
                first = sbin_cluster
            elif name == "ETC":
                first = etc_cluster
            elif name == "DEV":
                first = dev_cluster
            append_entries(image, root_off, cluster_bytes, entry_set(name, 0x10, first, 0))
        append_entries(image, root_off, cluster_bytes, entry_set("INIT", 0x20, init_first, init_size))

        f.seek(0)
        f.write(image)


if __name__ == "__main__":
    main()
