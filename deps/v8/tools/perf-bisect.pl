#!/usr/bin/perl
# Copyright 2024 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

=head Description

This script bisects a performance regression down to the commit responsible for the regression.


=head Install

You'll need to install Perl's Statistics::Test::WilcoxonRankSum
module. You can use these commands to do so:

    sudo apt install cpanminus # Install Perl's package manager
    cpanm -f Statistics::Test::WilcoxonRankSum # Install the module


=head Usage

    perl bisect.pl --start <start_hash> --end <end_hash> --run <benchmark_command>

Options:

    --start <start_hash>    At which commit to start the bisect
    --end <end_hash>        At which commit to end the bisect
    --run <command>         The command line to run the benchmark. Use V8 as a placeholder in there:
                            it will be replace by the path to the actual V8 being tested.
    --run-dir <dir>         The directory from which benchmarks should be ran: this script will `cd`
                            there to run the benchmarks.
    --compile <dir>         Which directory to compile. The default is `out/x64.release`.
    --perl-time             If specified, use Perl's timer to get the performance at each commit.
    --score-regex <regex>   If specified, use <regex> to extract the performance from the benchmark's
                            output. The regex is assumed to use a capture group to extract the result.
                            Either --perl-time or --score-regex should always be passed.
    --nb-run <num>          How many repetition for each benchmark for each commit. Default is 30.
    --retry <num>           How many times to retry benchmark at a given commit when bisect fails
                            because it doesn't find a statistical difference between the begining and
                            the end of the range.
    --verbose/--noverbose   Enables or disables verbose output. Default is true.

=head Example

I bisected the regressions at https://chromeperf.appspot.com/group_report?rev=85409 with:
(the offending CL was obvious, but this was just to showcase this script)

    perl bisect.pl --start b71cdae --end 54d255a --run "V8 --future cli.js -- ML" --run-dir v8-perf/v8-perf/benchmarks/JetStream2 --score-regex "Average-Score: (\d+)"


And that regression https://chromeperf.appspot.com/group_report?bug_id=1409635&project_id=chromium with:

    perl bisect.pl --start aa7b016 --end 0033691 --run "V8 run.js -- spread_literal/es6" --run-dir test/js-perf-test/SixSpeed --score-regex ": (\d+)"


=cut

use strict;
use warnings;
use feature qw(say);
use autodie qw(open close);

use List::Util qw(sum max);
use Statistics::Test::WilcoxonRankSum;
use Term::ANSIColor;
use Time::HiRes qw(time);
use Getopt::Long;
use File::Path qw(make_path remove_tree);
use Cwd qw(getcwd abs_path);

$| = 1; # auto-flusing


my $START = ""; # first commit
my $END = ""; # last commit
my $RUN_CMD = ""; # command to run to check perf
my $RUN_CMD_DIR = ""; # directory to run perf command from
my $COMPILE_DIR = "out/x64.release"; # Thing to compile
my $BISECT_DIR = "bisect";
my $PERL_TIME = 0; # If true, use Perl's timer for the comparison
my $SCORE_REGEX = ""; # If non-empty, use to extract score
my $NB_RUN = 30; # number of runs per measure
my $RETRY = 3; # if no statistical difference is found, retry $RETRY times.
my $VERBOSE = 1;


my $PROBA_THRESHOLD = 0.05; # Wilcoxon's threshold

GetOptions("start=s"       => \$START,
           "end=s"         => \$END,
           "run=s"         => \$RUN_CMD,
           "run-dir=s"     => \$RUN_CMD_DIR,
           "compile=s"     => \$COMPILE_DIR,
           "bisect-dir=s"  => \$BISECT_DIR,
           "perl-time"     => \$PERL_TIME,
           "score-regex=s" => \$SCORE_REGEX,
           "nb-run=i"      => \$NB_RUN,
           "retry=i"       => \$RETRY,
           "verbose!"      => \$VERBOSE);

my $START_DIR = "$BISECT_DIR/start";
my $MID_DIR = "$BISECT_DIR/mid";
my $END_DIR = "$BISECT_DIR/end";
my $LOG_FILE = abs_path("$BISECT_DIR/logs.txt");

sub trace {
  if ($VERBOSE) {
    print @_;
  }
  open my $FH, '>', $LOG_FILE;
  print $FH @_;
  close $FH;
}

sub usage {
  say "Usage:\n\t./$0 --start <start_commit> --end <end_commit> --run <run_cmd> [--perl-time|--score-regex <regex>]";
  exit();
}

if (! -d $BISECT_DIR) {
  make_path $BISECT_DIR;
}
if (-f $LOG_FILE) {
  unlink $LOG_FILE;
}

trace("Checking parameters...\n");
if (!$START || !$END || !$RUN_CMD) {
  my @missings = map { $_->[1] } grep { !$_->[0] }
    [$START, 'start'], [$END, 'end'], [$RUN_CMD, 'run'];
  say "Missing mandatory argument: ", join (", ", map { "--$_" } @missings);
  usage();
}
if (!-d $COMPILE_DIR) {
  say "Compile directory $COMPILE_DIR does not exist.";
  usage();
}
if ($RUN_CMD_DIR ne "" && !-d $RUN_CMD_DIR) {
  say "Run directory $RUN_CMD_DIR does not exist.";
  usage();
}
if (!$PERL_TIME && !$SCORE_REGEX) {
  say "One of --perl-time and --score-regex must be specified.";
  usage();
}
if ($PERL_TIME && $SCORE_REGEX) {
  say "Only of of --perl-time and --score-regex can be specified.";
  usage();
}

trace("Starting bisect...\n");

my ($start, $end) = ($START, $END);
my $compile_dir = getcwd();
while (1) {
  chdir $compile_dir;

  my $mid = get_middle_commit($start, $end);
  if (!$mid) {
    # $start and $end are consecutive commits.
    say colored("Bisection done.", 'bold'), " Regression happened at ", colored($end, 'red'), ": ", colored(get_commit_title($end), "italic");
    say "(previous commit: $start)";
    exit(1);
  }

  trace("\nBisecting between ", colored($start, 'green'), " and ",
        colored($end, "red"), " (middle = ", colored($mid, 'yellow'), ")\n");

  trace(colored("  Compiling...\n", 'bold'));
  if (-d $START_DIR) { remove_tree $START_DIR; }
  if (-d $MID_DIR) { remove_tree $MID_DIR; }
  if (-d $END_DIR) { remove_tree $END_DIR; }
  compile($start, $START_DIR, 'green');
  compile($mid, $MID_DIR, 'yellow');
  compile($end, $END_DIR, 'red');

  my $start_bin = abs_path("$START_DIR/d8");
  my $mid_bin = abs_path("$MID_DIR/d8");
  my $end_bin = abs_path("$END_DIR/d8");
  if ($RUN_CMD_DIR ne "") {
    chdir $RUN_CMD_DIR;
  }

  my $retry = 0;
 run:
  {
    trace(colored("  Running...\n", 'bold'));
    my %scores;
    for my $i (1 .. $NB_RUN) {
      for my $bin ($start_bin, $mid_bin, $end_bin) {
        trace("\r\033[2K    $i/$NB_RUN: $bin");
        my $time = time();
        my $cmd = $RUN_CMD =~ s/^V8/$bin/r;
        my $out = `$cmd`;
        if ($PERL_TIME) {
          push @{$scores{$bin}}, time() - $time;
        } else {
          my ($score) = $out =~ /$SCORE_REGEX/;
          push @{$scores{$bin}}, $score;
        }
      }
    }
    trace("\r\033[2K    All runs completed.\n");

    trace(colored("  Analyzing...\n", 'bold'));
    my ($start_avg, $start_stdev) = avg_and_stdev($scores{$start_bin});
    my ($mid_avg, $mid_stdev) = avg_and_stdev($scores{$mid_bin});
    my ($end_avg, $end_stdev) = avg_and_stdev($scores{$end_bin});
    trace("    Times:\n");
    trace("      start: $start_avg +- $start_stdev\n");
    trace("      mid: $mid_avg +- $mid_stdev\n");
    trace("      end: $end_avg +- $end_stdev\n");

    my $proba_start_mid = wilcoxon($scores{$start_bin}, $scores{$mid_bin});
    my $proba_mid_end = wilcoxon($scores{$mid_bin}, $scores{$end_bin});
    trace("    Proba:\n");
    trace("      start-mid: ", color_proba($proba_start_mid), "\n");
    trace("      mid-end: ", color_proba($proba_mid_end), "\n");
    if ($proba_start_mid < $PROBA_THRESHOLD && $proba_mid_end < $PROBA_THRESHOLD) {
      if ($retry++ == $RETRY) {
        say "Probabilities are $proba_start_mid and $proba_mid_end, which would indicate 2 regressions rather than 1, which is not supported by this script. Try to manually narrow the bisection range and rerun the script. Current range: $start - $mid - $end.";
        exit 1;
      } else {
        trace("    Two statistical differences (instead of one), re-running.\n");
        goto run;
      }
    }

    if ($proba_start_mid > $PROBA_THRESHOLD && $proba_mid_end > $PROBA_THRESHOLD) {
      if ($retry++ == $RETRY) {
        say "No statistical difference between $start, $mid and $end (after $RETRY retries). Aborting.";
        exit 1;
      } else {
        trace("    No statistical difference, re-running.\n");
        goto run;
      }
    }

    if ($proba_start_mid < $PROBA_THRESHOLD) {
      ($start, $end) = ($start, $mid);
    } else {
      ($start, $end) = ($mid, $end);
    }
  }
}


sub compile {
  my ($commit, $dst, $color) = @_;
  my $commit_title = get_commit_title($commit);
  trace("    Compiling at ", colored($commit, $color), ": ", colored($commit_title, 'italic'), "\n");
  system("git checkout $commit >>$LOG_FILE 2>&1") and die "Failed to checkout commit $commit";
  system("gclient sync >>$LOG_FILE 2>&1") and die "Failed to gclient sync";
  system("gn gen $COMPILE_DIR >>$LOG_FILE 2>&1") and die "Failed to gn gen";
  system("gn clean $COMPILE_DIR >>$LOG_FILE 2>&1") and die "Failed to clean $COMPILE_DIR";
  system("autoninja -C $COMPILE_DIR d8 >>$LOG_FILE 2>&1") and die "Failed to compile $COMPILE_DIR";
  system("cp -r $COMPILE_DIR $dst") and die "Failed to copy $COMPILE_DIR to $dst";
}

sub get_middle_commit {
  my ($start, $end) = @_;
  my $cmd = "git log --oneline $start..$end";
  # say "About to run: '$cmd'";
  # say "Current dir: ", getcwd();
  my @commits = map { s/ .*//r } split "\n", `$cmd`;
  shift @commits; # Removing $start
  if (!@commits) { return undef }
  return $commits[@commits/2];
}

sub avg_and_stdev {
  my $arr = shift;
  my $u = sum(@$arr)/@$arr; # mean
  my $s = ( sum( map {($_-$u)**2} @$arr ) / @$arr ) ** 0.5; # standard deviation
  return (sprintf("%.2f",$u), sprintf("%.2f",$s));
}

# Compute Wilcoxon Rank-Sum test between two datasets.
sub wilcoxon {
  my ($dataset1, $dataset2) = @_;
  my $wilcox_test = Statistics::Test::WilcoxonRankSum->new();
  $wilcox_test->load_data($dataset1, $dataset2);
  return $wilcox_test->probability();
}

sub color_proba {
  my $proba = shift;
  if ($proba < $PROBA_THRESHOLD) {
    return colored($proba, 'red');
  } else {
    return colored($proba, 'yellow');
  }
}

sub get_commit_title {
  my $commit = shift;
  my $commit_msg = `git show -s --format=%B $commit`;
  my ($title) = $commit_msg =~ /^(.*)$/m;
  return $title;
}
