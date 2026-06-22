#!/usr/bin/env perl
use strict;
use warnings;

my $out = shift @ARGV or die "usage: make_test_disk.pl OUTPUT\n";
my $sector_size = 512;
my $disk_sectors = 16384;
my $exfat_start = 2048;
my $exfat_sectors = 9216;
my $fat32_start = 11264;
my $fat32_sectors = 4096;

sub le16 { return pack("v", shift); }
sub le32 { return pack("V", shift); }
sub le64 {
    my ($v) = @_;
    return pack("V", $v & 0xffffffff) . pack("V", int($v / 4294967296));
}
sub write_at {
    my ($fh, $offset, $data) = @_;
    seek($fh, $offset, 0) or die "seek $offset: $!\n";
    print {$fh} $data;
}
sub lba_offset { return ($_[0] * $sector_size); }
sub short_entry {
    my ($name, $attr, $cluster, $size) = @_;
    my $entry = "\0" x 32;
    substr($entry, 0, 11, $name);
    substr($entry, 11, 1, chr($attr));
    substr($entry, 20, 2, le16(($cluster >> 16) & 0xffff));
    substr($entry, 26, 2, le16($cluster & 0xffff));
    substr($entry, 28, 4, le32($size));
    return $entry;
}
sub fat_short_checksum {
    my ($short_name) = @_;
    my $sum = 0;
    for my $byte (unpack("C*", $short_name)) {
        $sum = ((($sum & 1) ? 0x80 : 0) + ($sum >> 1) + $byte) & 0xff;
    }
    return $sum;
}
sub lfn_entry {
    my ($sequence, $name_part, $short_name) = @_;
    my @positions = (1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30);
    my $entry = "\0" x 32;
    my @chars = map { ord($_) } split(//, $name_part);
    substr($entry, 0, 1, chr($sequence));
    substr($entry, 11, 1, chr(0x0f));
    substr($entry, 13, 1, chr(fat_short_checksum($short_name)));
    for my $i (0 .. $#positions) {
        my $value = $i <= $#chars ? $chars[$i] : ($i == @chars ? 0x0000 : 0xffff);
        substr($entry, $positions[$i], 2, le16($value));
    }
    return $entry;
}
sub lfn_entries {
    my ($name, $short_name) = @_;
    my $entries = "";
    my $total = int((length($name) + 12) / 13);
    for (my $index = $total; $index >= 1; $index--) {
        my $sequence = $index;
        $sequence |= 0x40 if $index == $total;
        my $part = substr($name, ($index - 1) * 13, 13);
        $entries .= lfn_entry($sequence, $part, $short_name);
    }
    return $entries;
}

open my $fh, ">:raw", $out or die "open $out: $!\n";
print {$fh} "\0" x ($disk_sectors * $sector_size);

my $mbr = "\0" x $sector_size;
substr($mbr, 446, 16,
       "\0\0\0\0" . chr(0x07) . "\0\0\0" . le32($exfat_start) . le32($exfat_sectors));
substr($mbr, 462, 16,
       "\0\0\0\0" . chr(0x0c) . "\0\0\0" . le32($fat32_start) . le32($fat32_sectors));
substr($mbr, 510, 2, "\x55\xaa");
write_at($fh, 0, $mbr);

my $exfat = "\0" x $sector_size;
substr($exfat, 0, 11, "\xeb\x76\x90" . "EXFAT   ");
substr($exfat, 64, 8, le64($exfat_start));
substr($exfat, 72, 8, le64($exfat_sectors));
substr($exfat, 80, 4, le32(128));
substr($exfat, 84, 4, le32(64));
substr($exfat, 88, 4, le32(192));
substr($exfat, 92, 4, le32(1024));
substr($exfat, 96, 4, le32(2));
substr($exfat, 100, 4, "SOPT");
substr($exfat, 104, 2, le16(0x0100));
substr($exfat, 108, 5, "\x09\x03\x01\x80\x00");
substr($exfat, 510, 2, "\x55\xaa");
write_at($fh, lba_offset($exfat_start), $exfat);

my $exfat_base = lba_offset($exfat_start);
write_at($fh, $exfat_base + 128 * $sector_size + 8, le32(8));
write_at($fh, $exfat_base + 128 * $sector_size + 16, le32(7));
write_at($fh, $exfat_base + 128 * $sector_size + 24, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 28, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 32, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 40, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 44, le32(0xffffffff));

write_at($fh, $exfat_base + 192 * $sector_size, "\x85\x02\0\0\x20\0");
write_at($fh, $exfat_base + 192 * $sector_size + 32,
         "\xc0\0\0\x09\0\0\0\0" . le64(28) . "\0\0\0\0" . le32(3) . le64(28));
write_at($fh, $exfat_base + 192 * $sector_size + 64,
         "\xc1\0H\0E\0L\0L\0O\0.\0T\0X\0T\0");
write_at($fh, $exfat_base + 192 * $sector_size + 96, "\x85\x02\0\0\x20\0");
write_at($fh, $exfat_base + 192 * $sector_size + 128,
         "\xc0\0\0\x09\0\0\0\0" . le64(4128) . "\0\0\0\0" . le32(4) . le64(4128));
write_at($fh, $exfat_base + 192 * $sector_size + 160,
         "\xc1\0C\0H\0A\0I\0N\0.\0T\0X\0T\0");
write_at($fh, $exfat_base + 192 * $sector_size + 192, "\x05" x 3904);

write_at($fh, $exfat_base + 240 * $sector_size, "\x85\x02\0\0\x20\0");
write_at($fh, $exfat_base + 240 * $sector_size + 32,
         "\xc0\0\0\x08\0\0\0\0" . le64(22) . "\0\0\0\0" . le32(5) . le64(22));
write_at($fh, $exfat_base + 240 * $sector_size + 64,
         "\xc1\0L\0A\0T\0E\0.\0T\0X\0T\0");
write_at($fh, $exfat_base + 240 * $sector_size + 96, "\x85\x02\0\0\x10\0");
write_at($fh, $exfat_base + 240 * $sector_size + 128,
         "\xc0\x02\0\x03\0\0\0\0" . le64(4096) . "\0\0\0\0" . le32(6) . le64(4096));
write_at($fh, $exfat_base + 240 * $sector_size + 160, "\xc1\0D\0I\0R\0");
write_at($fh, $exfat_base + 240 * $sector_size + 192,
         "\x81\0" . ("\0" x 18) . le32(10) . le64(128));
write_at($fh, $exfat_base + 240 * $sector_size + 224,
         "\x82\0" . ("\0" x 18) . le32(11) . le64(32));

write_at($fh, $exfat_base + 224 * $sector_size, "\x85\x02\0\0\x20\0");
write_at($fh, $exfat_base + 224 * $sector_size + 32,
         "\xc0\0\0\x09\0\0\0\0" . le64(28) . "\0\0\0\0" . le32(9) . le64(28));
write_at($fh, $exfat_base + 224 * $sector_size + 64,
         "\xc1\0I\0N\0N\0E\0R\0.\0T\0X\0T\0");

write_at($fh, $exfat_base + 200 * $sector_size, "Hello from exFAT root file.\n");
write_at($fh, $exfat_base + 208 * $sector_size, "A" x 4096);
write_at($fh, $exfat_base + 216 * $sector_size, "Late root chain file.\n");
write_at($fh, $exfat_base + 232 * $sector_size, "B" x 32);
write_at($fh, $exfat_base + 248 * $sector_size, "Hello from an exFAT subdir.\n");
write_at($fh, $exfat_base + 256 * $sector_size, "\xff\x03");
write_at($fh, $exfat_base + 264 * $sector_size, "TangPingOS exFAT upcase table.");

my $fat32 = "\0" x $sector_size;
substr($fat32, 0, 11, "\xeb\x58\x90" . "MSDOS5.0");
substr($fat32, 11, 2, le16(512));
substr($fat32, 13, 1, chr(1));
substr($fat32, 14, 2, le16(32));
substr($fat32, 16, 1, chr(1));
substr($fat32, 21, 1, chr(0xf8));
substr($fat32, 24, 2, le16(32));
substr($fat32, 26, 2, le16(64));
substr($fat32, 28, 4, le32($fat32_start));
substr($fat32, 32, 4, le32($fat32_sectors));
substr($fat32, 36, 4, le32(16));
substr($fat32, 44, 4, le32(2));
substr($fat32, 48, 2, le16(1));
substr($fat32, 50, 2, le16(6));
substr($fat32, 64, 1, chr(0x80));
substr($fat32, 66, 1, chr(0x29));
substr($fat32, 67, 4, le32(0x54414e47));
substr($fat32, 71, 11, "TANGBOOT   ");
substr($fat32, 82, 8, "FAT32   ");
substr($fat32, 510, 2, "\x55\xaa");
write_at($fh, lba_offset($fat32_start), $fat32);

my $fsinfo = "\0" x $sector_size;
substr($fsinfo, 0, 4, "RRaA");
substr($fsinfo, 484, 4, "rrAa");
substr($fsinfo, 488, 4, le32(4000));
substr($fsinfo, 492, 4, le32(5));
substr($fsinfo, 510, 2, "\x55\xaa");
write_at($fh, lba_offset($fat32_start + 1), $fsinfo);

my $fat = "\0" x (16 * $sector_size);
substr($fat, 0, 4, le32(0x0ffffff8));
substr($fat, 4, 4, le32(0xffffffff));
substr($fat, 8, 4, le32(0x0fffffff));
substr($fat, 12, 4, le32(0x0fffffff));
substr($fat, 16, 4, le32(0x0fffffff));
substr($fat, 20, 4, le32(0x0fffffff));
substr($fat, 24, 4, le32(0x0fffffff));
substr($fat, 28, 4, le32(0x0fffffff));
substr($fat, 32, 4, le32(0x0fffffff));
write_at($fh, lba_offset($fat32_start + 32), $fat);

my $readme = "Hello from FAT32 boot partition.\n";
my $kernel = "This FAT32 sample stands in for a UEFI ESP.\n";
my $bootx64 = "TangPingOS can now walk FAT32 EFI/BOOT.\n";
my $long_name = "FAT32 multi-entry long file names are visible now.\n";
my $root = "\0" x $sector_size;
substr($root, 0, 32, short_entry("README  TXT", 0x20, 3, length($readme)));
substr($root, 32, 32, short_entry("KERNEL  TXT", 0x20, 4, length($kernel)));
substr($root, 64, 32, short_entry("EFI        ", 0x10, 5, 0));
substr($root, 96, 64, lfn_entries("very long filename.txt", "VERYLO~1TXT"));
substr($root, 160, 32, short_entry("VERYLO~1TXT", 0x20, 8, length($long_name)));
my $efi_dir = "\0" x $sector_size;
substr($efi_dir, 0, 32, short_entry("BOOT       ", 0x10, 6, 0));
my $boot_dir = "\0" x $sector_size;
substr($boot_dir, 0, 32, short_entry("BOOTX64 EFI", 0x20, 7, length($bootx64)));
write_at($fh, lba_offset($fat32_start + 48), $root);
write_at($fh, lba_offset($fat32_start + 49), $readme);
write_at($fh, lba_offset($fat32_start + 50), $kernel);
write_at($fh, lba_offset($fat32_start + 51), $efi_dir);
write_at($fh, lba_offset($fat32_start + 52), $boot_dir);
write_at($fh, lba_offset($fat32_start + 53), $bootx64);
write_at($fh, lba_offset($fat32_start + 54), $long_name);

close $fh or die "close $out: $!\n";
