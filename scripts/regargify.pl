#!/usr/bin/perl -w

use strict;

die "Need a file to act upon." unless @ARGV;

my $file = $ARGV[0];

my $first_init_buf = "";
my $init_buf = "";
my $after_buf = "";
my $seen_module_init = 0;
my $write_to_after_buf = 0;

open FH, '<', $file;

while (<FH>)
{
	if (/int module_init/) { $seen_module_init = 1; }
	if (/return 0/ and !$write_to_after_buf) { $write_to_after_buf = 1; }

	if (!$seen_module_init)
	{
		$first_init_buf .= $_;
	} elsif (!$write_to_after_buf)
	{
		$init_buf .= $_;
	} else {
		$after_buf .= $_;
	}
}

my @args;
my $new_after_buf = "";

# first pass
foreach (split /\n/, $after_buf)
{
	if (/([^[:space:]]*) = mod->GetArg\(node, ?"([^"]*)"/)
	{
		(my $n = $1) =~ y/a-z/A-Z/;
		push @args, { var => $n, arg => $2 };
		$new_after_buf .= "\t$1 = mod->GetArg(node, args[$n]);\n";
	}
	else
	{
		$new_after_buf .= "$_\n";
	}
}

print $first_init_buf;

print "enum { " . join(',', map { $_->{'var'} } @args) . " };\n\n";
print "int args[" . $args[$#args]->{'var'} . " + 1];\n\n";

print $init_buf;

foreach (@args)
{
	my %f = %{$_};
	print "\targs[" . $f{'var'} . "] = plugin->RegArg(\"" . $f{'arg'} . "\");\n";
}

print $new_after_buf;
