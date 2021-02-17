#!/usr/bin/perl
use strict;
use warnings;
use autodie;
use Time::HiRes qw(sleep);
use POSIX qw(strftime);

if (!@ARGV or $ARGV[0] ne "--please-destroy-my-data") {
    print "DANGER! Please read and understand the source code before using.\n";
    print "Regular users do not need to use this program. Use 'pio run' instead.\n";
    exit 1;
}

my %pane;
my $command = "";

sub printlog {
    print strftime("%Y-%m-%d %H:%M:%S", localtime), " @_\n";
}

sub spawn {
    my ($dev) = @_;

    printlog "Spawning $dev";

    my $c = $command;

    $c =~ s[ttyUSB\w+][$dev];
    $c =~ s/^/time /;
    $c =~ s[$][| perl -pe"BEGIN { \$/ = \\1 } s/\\r/\\n/"; echo "-- $dev done --"; read a];

    $pane{$dev} = readpipe qq{tmux split-window -d -h -P -F '#{pane_id}' '$c'};
    chomp $pane{$dev};

    system "tmux select-layout even-horizontal";
}

if (1 < (() = glob("/dev/ttyUSB*"))) {
    die "Disconnect USB serial devices except one target device.\n";
}

if ($ARGV[-1] ne "it's-a-me") {
    exec qw[tmux new perl], $0, @ARGV, "it's-a-me";
}

my $pio_pid = open my $pio, "-|", "pio run -v -t upload";
while (defined(my $line = readline $pio)) {
    print $line;
    if ($line =~ /esptool/ && $line =~ /write_flash/) {
        $command = $line;
        system "pkill -9 -P $pio_pid";  # kill upload process.
        kill 9, $pio_pid;
        last;
    }
}

if ($command =~ /(ttyUSB\d+)/) {
    spawn $1;
} else {
    warn "Could not snatch upload command.\nMake sure a single target device is already connected.\nPress enter to exit.\n";
    scalar <STDIN>;
    die;
}

open my $monitor, "-|", "udevadm monitor --kernel --subsystem-match=usb-serial";

printlog "Started monitoring for new usb-serial devices.";


while (defined(my $line = readline $monitor)) {
    my ($event) = $line =~ /\b(remove|add)\b/ or next;
    my ($dev) = $line =~ /(ttyUSB\d+)/ or next;

    if ($event eq 'add') {
        while (!-w "/dev/$dev") {
            sleep .1;
            # wait for permissions to settle;
        }

        spawn($dev);
    } else {
        if (exists $pane{$dev}) {
            printlog "Killing $dev";
            system "tmux kill-pane -t $pane{$dev}";
        }
    }
}
