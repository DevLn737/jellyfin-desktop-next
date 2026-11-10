#!/usr/bin/perl
# The Live TV stand's "tuner": a tiny HTTP server inside the test container
# (127.0.0.1 only) that sends the generated MPEG-TS files at about their own
# bit rate, as an IPTV source would, and then sends them again from the
# start, so a channel never ends during a test. perl-base modules only: the
# Jellyfin image has perl-base but no Python. Never exposed outside the
# container.
#
#   perl tuner.pl <port> <directory with stream-*.ts>
use strict;
use warnings;
use IO::Socket::INET;

# perl-base has no Time::HiRes: whole seconds for the clock, select() for
# short waits.
sub pause { my ($seconds) = @_; select(undef, undef, undef, $seconds); }

my ($port, $root) = @ARGV;
die "usage: tuner.pl <port> <dir>\n" unless $port && $root;
$SIG{CHLD} = 'IGNORE';
$SIG{PIPE} = 'IGNORE';

my $server = IO::Socket::INET->new(LocalAddr => '127.0.0.1', LocalPort => $port, Listen => 16,
                                   ReuseAddr => 1, Proto => 'tcp') or die "listen: $!\n";

# The bit rate of a file, from the duration written beside it
# (stream-N.ts.seconds); without it the file goes out as fast as it can.
sub rate_of {
  my ($file) = @_;
  open(my $fh, '<', "$file.seconds") or return 0;
  my $seconds = <$fh>;
  close $fh;
  return 0 unless $seconds && $seconds > 0;
  return (-s $file) / $seconds;
}

while (my $client = $server->accept) {
  my $pid = fork;
  if (!defined $pid) { close $client; next; }
  if ($pid) { close $client; next; }
  close $server;
  $client->autoflush(1);
  my $request = <$client> // '';
  while (my $line = <$client>) { last if $line =~ /^\r?\n$/; }
  # /stream-1/channel-7.ts: channel 7, playing stream-1.ts. Every channel
  # has an address of its own, since the server tells M3U channels apart by
  # their address.
  my ($method, $stream) = $request =~ m{^(GET|HEAD) /(stream-\d+)/channel-\d+\.ts[ ?]};
  my $file = defined $stream ? "$root/$stream.ts" : '';
  if (!$method || !-f $file) {
    print $client "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    exit 0;
  }
  print $client "HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nConnection: close\r\n\r\n";
  exit 0 if $method eq 'HEAD';
  my $rate = rate_of($file);
  my $start = time;
  my $sent = 0;
  # Ahead of real time by a few seconds, as a tuner's buffer would be.
  my $lead = $rate * 3;
  while (1) {
    open(my $fh, '<:raw', $file) or exit 0;
    my $chunk;
    while (read($fh, $chunk, 65536)) {
      print $client $chunk or exit 0;
      $sent += length $chunk;
      if ($rate > 0) {
        my $due = ($sent - $lead) / $rate - (time - $start);
        pause($due) if $due > 0;
      }
    }
    close $fh;
  }
}
