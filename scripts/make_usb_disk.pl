#!/usr/bin/env perl
use strict;
use warnings;

my $out = shift @ARGV or die "usage: make_usb_disk.pl OUTPUT\n";
my $sector_size = 512;
my $sectors_per_cluster = 8;
my $cluster_size = $sector_size * $sectors_per_cluster;
my $disk_sectors = 32768;
my $exfat_start = 2048;
my $exfat_sectors = $disk_sectors - $exfat_start;

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
sub exfat_name_entry {
    my ($name) = @_;
    my $entry = "\xc1\0";
    my @chars = split(//, $name);
    for my $i (0 .. 14) {
        my $ch = $i <= $#chars ? ord($chars[$i]) : 0;
        $entry .= le16($ch);
    }
    return substr($entry . ("\0" x 32), 0, 32);
}
sub exfat_file_entries {
    my ($name, $cluster, $content) = @_;
    my $size = length($content);
    my $name_len = length($name);
    my $file = "\x85\x02\0\0\x20\0" . ("\0" x 26);
    my $stream = "\xc0\0\0" . chr($name_len) . ("\0" x 4) .
                 le64($size) . ("\0" x 4) . le32($cluster) . le64($size);
    return $file . $stream . exfat_name_entry($name);
}

open my $fh, ">:raw", $out or die "open $out: $!\n";
print {$fh} "\0" x ($disk_sectors * $sector_size);

my $mbr = "\0" x $sector_size;
substr($mbr, 446, 16,
       "\0\0\0\0" . chr(0x07) . "\0\0\0" . le32($exfat_start) . le32($exfat_sectors));
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
substr($exfat, 100, 4, "USBT");
substr($exfat, 104, 2, le16(0x0100));
substr($exfat, 108, 5, "\x09\x03\x01\x80\x00");
substr($exfat, 510, 2, "\x55\xaa");
write_at($fh, lba_offset($exfat_start), $exfat);

my $exfat_base = lba_offset($exfat_start);
write_at($fh, $exfat_base + 128 * $sector_size + 8, le32(8));
write_at($fh, $exfat_base + 128 * $sector_size + 16, le32(7));
write_at($fh, $exfat_base + 128 * $sector_size + 20, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 28, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 32, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 40, le32(0xffffffff));
write_at($fh, $exfat_base + 128 * $sector_size + 44, le32(0xffffffff));

my $hello = "Hello from USB BOT exFAT.\n";
my $chain = "USB block device cluster chain works.\n" . ("U" x 4096);
my $late = "Late USB root entry.\n";

my $root0 = "\x05" x $cluster_size;
substr($root0, 0, 96, exfat_file_entries("HELLO.TXT", 3, $hello));
substr($root0, 96, 96, exfat_file_entries("CHAIN.TXT", 4, $chain));
my $root1 = "\0" x $cluster_size;
substr($root1, 0, 96, exfat_file_entries("LATE.TXT", 5, $late));
substr($root1, 96, 32, "\x81\0" . ("\0" x 18) . le32(10) . le64(128));
substr($root1, 128, 32, "\x82\0" . ("\0" x 18) . le32(11) . le64(32));

write_at($fh, $exfat_base + 192 * $sector_size, $root0);
write_at($fh, $exfat_base + 240 * $sector_size, $root1);
write_at($fh, $exfat_base + 200 * $sector_size, $hello);
write_at($fh, $exfat_base + 208 * $sector_size, substr($chain, 0, 4096));
write_at($fh, $exfat_base + 216 * $sector_size, $late);
write_at($fh, $exfat_base + 232 * $sector_size, substr($chain, 4096));
write_at($fh, $exfat_base + 256 * $sector_size, "\xff\x03");
write_at($fh, $exfat_base + 264 * $sector_size, "TangPingOS USB exFAT upcase table.");

close $fh or die "close $out: $!\n";
