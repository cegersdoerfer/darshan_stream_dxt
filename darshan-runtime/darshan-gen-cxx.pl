#!/usr/bin/perl -w
#
# Copyright (C) 2015 University of Chicago.
# See COPYRIGHT notice in top-level directory.
#

# takes a standard mpicxx script as an argument and tried to generate a
# darshan-enabled mpicxx script to mimic it's behavior

use Getopt::Long;
use English;

my $PREFIX="/usr/local";

my $input_file = "";
my $output_file = "";
my $trim_exe_path = 0;
my $xl_check = 0;

process_args();

# run original mpicc with -show argument to capture command line for
# compilation and linking
my $compile_cmd = `$input_file -show -c foo.c`;
if (!($compile_cmd))
{
    printf STDERR "Error: failed to invoke $input_file with -show\n";
    exit(1);
}

my $link_cmd = `$input_file -show foo.o -o foo`;
if (!($link_cmd))
{
    printf STDERR "Error: failed to invoke $input_file with -show\n";
    exit(1);
}

# we just want the first line of output from the mpi script's -v argument;
# the rest we assume will be generated by the underlying compiler
my $version_out = `$input_file -v 2>/dev/null |head -n 1`;
if (!($version_out))
{
    printf STDERR "Error: failed to invoke $input_file with -v\n";
    exit(1);
}

# check command lines for accuracy
if(!($compile_cmd =~ /-c foo.c/) || !($link_cmd =~ /foo.o -o foo/))
{
    printf STDERR "Error: faulty output from $input_file with -show.\n";
    exit(1);
}
chomp($compile_cmd);
chomp($link_cmd);
chomp($version_out);

#
# Incomprehensible re to eat the leading path of the compiler command
# and only give the basename.
#
# /cmd -> cmd
# cmd -> cmd
# /x/y/x/z/cmd -> cmd
#
if($trim_exe_path)
{
    $compile_cmd =~ s/\/*([^\/ ]+\/)*//;
    $link_cmd =~ s/\/*([^\/ ]+\/)*//;
}

open (OUTPUT, ">$output_file") || die("Error opening output file: $!");

# split the link command from the original mpicc script so that we have the
# part before the "foo.o -o foo", which is assumed to be CXX, and the part
# that comes afterwards, which is assumed to be the required link flags and
# libraries.
if(!($link_cmd =~ /(\S+)(.+)(foo.o -o foo)\s+(.*)/))
{
    printf STDERR "Error: improperly formatted link cmd: $link_cmd\n";
    exit(1);
}
$CXX_from_link=$1;
$link_cmd_prefix=$2;
$base_link_cmd_suffix="\"\$\{allargs\[\@\]\}\" $4";
$link_cmd_suffix="\"\$\{newallargs\[\@\]\}\" `$PREFIX/bin/darshan-config --pre-ld-flags` $4 `$PREFIX/bin/darshan-config --post-ld-flags`";
$dyn_link_cmd_suffix="\"\$\{newallargs\[\@\]\}\" `$PREFIX/bin/darshan-config --dyn-ld-flags` $4";

# repeat the above step for the compilation command line
if(!($compile_cmd =~ /(\S+)(.+)(-c foo.c)\s+(.*)/))
{
    printf STDERR "Error: improperly formatted compile cmd: $compile_cmd\n";
    exit(1);
}
$CXX_from_compile=$1;
$compile_cmd_prefix=$2;
$compile_cmd_suffix="\"\$\{allargs\[\@\]\}\" $4";

# we expect to see the same CXX command in both the link and compile steps
if($CXX_from_link ne $CXX_from_compile)
{
    printf STDERR "Error: cannot find matching CXX from: $compile_cmd\n";
    printf STDERR "and: $link_cmd\n";
    exit(1);
}
$CXX=$CXX_from_compile;

# create link cmd with noshrargs variable
$link_cmd_no_shared_suffix = $base_link_cmd_suffix;
$link_cmd_no_shared_suffix =~ s/allargs/noshrargs/;

print OUTPUT<<"EOF";
#!/bin/bash
#
# Auto-generated mpicc script from darshan-gen-cc.pl
#
#
# Internal variables
# Show is set to echo to cause the compilation command to be echoed instead 
# of executed.
Show=
CXX=\${MPICH_CXX:-"$CXX"}

EOF

if ($xl_check == 1)
{
print OUTPUT<<'EOF';
#
# Check for IBM XL Linker Parameters
#
# If IPA linker is used, we must disable darshan
# because the IPA linker doesn't support -wrap.
#
function check_XL()
{
    arg=$1;
    ipa_enabled=0;

    if [ "$arg" = "-O5" ]; then
        ipa_enabled=1;
    fi
    if [ "$arg" = "-O4" ]; then
        ipa_enabled=1;
    fi
    if [ "$arg" = "-qipa" ]; then
        ipa_enabled=1;
    fi

    return $ipa_enabled;
}
EOF
}

print OUTPUT<<'EOF';
linking=yes
allargs=("$@")
argno=0
for arg in "$@" ; do
    # Set addarg to no if this arg should be ignored by the C compiler
    addarg=yes
    case "$arg" in 
 	# ----------------------------------------------------------------
	# Compiler options that affect whether we are linking or no
    -c|-S|-E|-M|-MM)
    # The compiler links by default
    linking=no
    ;;
    -v)
    # Pass this argument to the compiler as well.
EOF
print OUTPUT<<"EOF";
    echo "$version_out"
EOF
print OUTPUT<<'EOF';
    # if there is only 1 argument, it must be -v.
    if [ "$#" -eq "1" ] ; then
        linking=no
    fi
    ;;
    -cxx=*)
    CXX=`echo A$arg | sed -e 's/A-cxx=//g'`
    addarg=no
    ;;
    -show)
    addarg=no
    Show=echo
    ;;
    esac
    if [ $addarg = no ] ; then
	unset allargs[$argno]
    fi
    # Some versions of bash do not accept ((argno++))
    argno=`expr $argno + 1`
done
if [ "$linking" = yes ] ; then
    if [ -n "$C_LINKPATH_SHL" ] ; then
	# prepend the path for the shared libraries to the library list
	mpilibs="$C_LINKPATH_SHL$libdir $mpilibs"
    fi
EOF
print OUTPUT<<"EOF";

    # if allargs includes any libraries, then we need to get
    # -ldarshan in there first
    argno=0;
    once=0;
    compiler_check=0;
    newallargs=\("\$\{allargs\[\@\]\}"\);
    for arg in "\$\{newallargs\[\@\]\}"; do
        res=`expr match "\$arg" "-l"`;
        if [ \$res -gt 0 -a \$once -lt 1 ]; then
            newallargs[\$argno]=-ldarshan
            argno=`expr \$argno + 1`
            newallargs[\$argno]=\$arg;
            once=1;
        else
            newallargs[\$argno]=\$arg;
        fi
EOF

#
# Add any functional tests for linker parameters
#
       if ($xl_check)
       {
print OUTPUT<<"EOF";
           check_XL "\$arg";
           if [ \$? -ne 0 ]; then
               compiler_check=1;
           fi
EOF
       }

print OUTPUT<<"EOF";

        argno=`expr \$argno + 1`
    done

    used_darshan=0

    # Perform a test link before the real link.  This allows us to check
    # for two things:
    # 1) Are MPI symbols present? Technically Darshan should not harm
    #    non-MPI programs, but it may bring in unwanted dependencies or
    #    interfere with autoconf checks.
    # 2) Is this link command line going to produce a static or dynamic
    #    linked executable?  We will adjust Darshan link strategy accordingly.

    # create a temporary file
    tmpfile=`mktemp`
    binfile=`mktemp`
    # generate a map of the symbol names
    # don't use -shared for this step
    argno=0
    noshrargs=\("\$\{allargs\[\@\]\}");
    for arg in "\$\{noshrargs\[\@\]\}"; do
        if [ "\$arg" = "-shared" ]; then
            unset noshrarg[\$argno];
        fi
        argno=`expr \$argno + 1`
    done
    \$CXX $link_cmd_prefix $link_cmd_no_shared_suffix -Wl,-Map,\$tmpfile \$LDFLAGS -o \$binfile >& /dev/null

    # is MPI in there?
    grep MPI \$tmpfile >& /dev/null
    rc_mpi=\$?

    # did the link options produce a dynamic executable?
    # we look for dynamic MPI::Init symbol to confirm, but we have to be wary of mangling...
    nm --demangle --dynamic \$binfile | grep "MPI::Init" >& /dev/null
    rc_dyn=\$?

    rm \$tmpfile >& /dev/null
    rm \$binfile >& /dev/null

    # disable darshan if the executable is not an MPI program or we've
    # detected an incompatible compiler
    if [ \$rc_mpi -eq 0 ] && [ \$compiler_check -eq 0 ]; then
        if [ \$rc_dyn -eq 0 ]; then
            \$Show \$CXX $link_cmd_prefix $dyn_link_cmd_suffix
        else
            \$Show \$CXX $link_cmd_prefix $link_cmd_suffix
        fi
        used_darshan=1
    fi

    # otherwise use the normal command line
    if [ "\$used_darshan" = "0" ] ; then
        \$Show \$CXX $link_cmd_prefix $base_link_cmd_suffix
    fi
    rc=\$?
else
    \$Show \$CXX $compile_cmd_prefix $compile_cmd_suffix
    rc=\$?
fi

exit \$rc
EOF

close(OUTPUT);

chmod(0755, $output_file); 

exit(0);

sub process_args
{
    use vars qw( $opt_help $opt_output $opt_trim $opt_xl );

    Getopt::Long::Configure("no_ignore_case", "bundling");
    GetOptions( "help",
        "output=s",
        "trim",
        "xl");

    if($opt_help)
    {
        print_help();
        exit(0);
    }

    if($opt_trim)
    {
        $trim_exe_path=1;
    }

    if($opt_xl)
    {
        $xl_check=1;
    }

    if($opt_output)
    {
        $output_file = $opt_output;
    }
    else
    {
        print_help();
        exit(1);
    }

    # there should only be one remaining argument: the input file 
    if($#ARGV != 0)
    {
        print "Error: invalid arguments.\n";
        print_help();
        exit(1);
    }
    $input_file = $ARGV[0];

    return;
}

sub print_help
{
    print<<"EOF";

Usage: $PROGRAM_NAME <stock mpicxx> --output <modified mpicc>

    --help          Prints this help message
    --output        Specifies name of output script
    --trim          Trim the compilers path
    --xl            Disables darshan if certain linker parameters are used
                    (Intended for IBM XL Compilers)

Purpose:

    This script takes an existing mpicxx script as input and generates a
    modified version that includes Darshan support.

EOF
    return;
}

