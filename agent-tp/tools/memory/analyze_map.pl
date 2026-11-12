#!/usr/bin/env perl
use strict;
use warnings;
use FindBin qw($Bin);
use File::Spec;

# Analyze allocated output sections, excluding discarded input sections.
# Count each address once: merged strings can have overlapping input entries.
my $root = File::Spec->rel2abs("$Bin/../../..");
my $path = shift @ARGV // "$root/build/EVT_ESP32-S3.map";
open my $fh, '<', $path or die "$path: $!\n";
my @sections;
my $current;
my $pending;
while (my $line = <$fh>) {
    $line =~ s/\r?\n$//;
    if ($line =~ /^(\.[^\s]+)\s+(0x[\da-f]+)\s+(0x[\da-f]+)/i) {
        my ($name, $base, $length) = ($1, hex($2), hex($3));
        $current = undef;
        $pending = undef;
        if ($name =~ /^(?:\.iram0\.(?:vectors|text)|\.dram0\.(?:data|bss)|\.ext_ram\.bss)$/) {
            $current = { name => $name, base => $base, size => $length, inputs => [] };
            push @sections, $current;
        }
        next;
    }
    next unless $current;
    if ($line =~ /^ (\.[^\s]+)\s*$/) {
        $pending = $1;
        next;
    }
    my ($symbol, $addr, $size, $object);
    if ($line =~ /^ (\.[^\s]+)\s+(0x[\da-f]+)\s+(0x[\da-f]+)\s+(.+\.o(?:bj)?\)?)/i) {
        ($symbol, $addr, $size, $object) = ($1, hex($2), hex($3), $4);
    } elsif (defined($pending) && $line =~ /^\s+(0x[\da-f]+)\s+(0x[\da-f]+)\s+(.+\.o(?:bj)?\)?)/i) {
        ($symbol, $addr, $size, $object) = ($pending, hex($1), hex($2), $3);
    } else {
        $pending = undef;
        next;
    }
    $pending = undef;
    next unless $size;
    my $owner = $object;
    $owner = $1 if $object =~ /(lib[^\/\\()]+\.a)/;
    push @{$current->{inputs}}, {symbol => $symbol, addr => $addr, size => $size,
                                object => $object, owner => $owner};
}
close $fh;
print "# 链接内存分解\n\n输入：`$path`。只统计实际输出段，不含丢弃段。单位为字节。\n\n";
print "归属按地址去重；跨组件重叠的合并常量单列为 shared/merged，未被输入条目覆盖的地址列为 padding/unattributed。对象表为输入条目声明大小，可能包含重叠，不能直接相加。\n\n";
for my $section (@sections) {
    my @owners;
    for my $input (@{$section->{inputs}}) {
        my $lo = $input->{addr} - $section->{base};
        my $hi = $lo + $input->{size};
        next if $hi <= 0 || $lo >= $section->{size};
        $lo = 0 if $lo < 0;
        $hi = $section->{size} if $hi > $section->{size};
        for my $i ($lo .. $hi - 1) {
            if (!defined $owners[$i]) { $owners[$i] = $input->{owner}; }
            elsif ($owners[$i] ne $input->{owner}) { $owners[$i] = 'shared/merged'; }
        }
    }
    my %totals;
    for my $i (0 .. $section->{size} - 1) {
        $totals{$owners[$i] // 'padding/unattributed'}++;
    }
    my $sum = 0; $sum += $_ for values %totals;
    die "Accounting mismatch\n" unless $sum == $section->{size};
    printf "## %s\n\n起始 `0x%X`，大小 %d B（%.3f KiB）。\n\n", $section->{name}, $section->{base}, $section->{size}, $section->{size}/1024;
    print "| 组件 | B | KiB |\n| --- | ---: | ---: |\n";
    for my $owner (sort {$totals{$b} <=> $totals{$a}} keys %totals) {
        printf "| %s | %d | %.3f |\n", $owner, $totals{$owner}, $totals{$owner}/1024;
    }
    print "\n最大输入条目：\n\n| 条目 | 声明大小 B | 来源 |\n| --- | ---: | --- |\n";
    my @top = sort {$b->{size} <=> $a->{size}} @{$section->{inputs}};
    splice @top, 15 if @top > 15;
    for my $input (@top) {
        print "| `$input->{symbol}` | $input->{size} | `$input->{object}` |\n";
    }
    print "\n" unless $section == $sections[-1];
}
