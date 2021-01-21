#!/bin/bash

sys_info() {
    # Output information to assist in bug reports
    if test -z "$UPCXX_INSTALL_QUIET" ; then (
        if test -d .git ; then
            echo UPCXX revision: `git describe --always 2> /dev/null`
        fi
        echo System: `uname -a 2>&1`
        /usr/bin/sw_vers 2> /dev/null
        /usr/bin/xcodebuild -version 2> /dev/null 
        /usr/bin/lsb_release -a 2> /dev/null
        echo " "
        echo Date: `date 2>&1`
        echo Current directory: `pwd 2>&1`
        echo Install directory: $PREFIX
        echo "Configure command$fullcmd" # fullcmd starts w/ a colon
        local SETTINGS=
        for var in ${UPCXX_CONFIG_ENVVARS[*]/#/ORIG_} ; do
            if test "${!var:+set}" = set; then
                SETTINGS+="    ${var#ORIG_}='${!var}'\n"
            fi
        done
        echo -n -e "Configure environment:\n$SETTINGS"
        echo " "
    ) fi
}

# For probing for lowest acceptable (for its libstdc++) g++ version.
# These are defaults, which may be overridden per-platform (such as Cray XC).
MIN_GNU_MAJOR=6
MIN_GNU_MINOR=4
MIN_GNU_PATCH=0
MIN_GNU_STRING='6.4'

# Run CC or CXX to determine what __GNUC__,__GNUC_MINOR__,__GNUC_PATCHLEVEL__ it reports.
# First argument is CC or CXX (literal)
# Second (optional) argument is an actual compiler command to use in place of CC or CXX
# Results are in gnu_version and gnu_{major,minor,patch} upon return.
# Returns:
#   0 - success
#   1 - identified too-low version
#   other - failed to identify version
check_gnu_version() {
    case $1 in
         CC) local suffix=c   compiler="${2:-$CC $CFLAGS}" ;;
        CXX) local suffix=cpp compiler="${2:-$CXX $CXXFLAGS}";;
          *) echo Internal error; exit 1;;
    esac
    trap "rm -f conftest.$suffix" RETURN
    local TOKEN1='_MKkiiTv4jDk8Tmw6_'
    local TOKEN2='_SDPECv3TjARP7xiZ_'
    cat >conftest.$suffix <<_EOF
      #undef  _REPORT
      #undef  _REPORT_HELPER
      #define _REPORT(a,b,c) _REPORT_HELPER(a,b,c)
      #define _REPORT_HELPER(a,b,c) $TOKEN1 ## a ## _ ## b ## _ ## c ## $TOKEN2
      _REPORT(__GNUC__,__GNUC_MINOR__,__GNUC_PATCHLEVEL__)
_EOF
    if ! [[ $(eval $compiler -E conftest.$suffix) =~ ${TOKEN1}([0-9]+)_([0-9]+)_([0-9]+)${TOKEN2} ]]; then
        echo "ERROR: regex match failed probing \$$1 for GNUC version"
        return 2
    fi
    gnu_major=${BASH_REMATCH[1]}
    gnu_minor=${BASH_REMATCH[2]}
    gnu_patch=${BASH_REMATCH[3]}
    gnu_version="$gnu_major.$gnu_minor.$gnu_patch"
    return $(( (    gnu_major*1000000 +     gnu_minor*1000 +     gnu_patch) <
               (MIN_GNU_MAJOR*1000000 + MIN_GNU_MINOR*1000 + MIN_GNU_PATCH) ))
}

# extract gcc-name or gxx-name from an Intel compiler
# on success, returns zero and yields the string value on stdout
#             may emit warning(s) on stderr
# on failure, returns non-zero and yields an error message on stdout
#
# precondition: $gnu_version must be set as by a preceeding 'check_gnu_version CXX'
get_intel_name_option() {
    case $1 in
         CC) local suffix=c   option=-gcc-name exe=gcc compiler="$CC $CFLAGS" ;;
        CXX) local suffix=cpp option=-gxx-name exe=g++ compiler="$CXX $CXXFLAGS";;
          *) echo Internal error; exit 1;;
    esac
    trap "rm -f conftest.$suffix conftest.o" RETURN
    echo >conftest.$suffix
    local output result
    output="$(eval $compiler -v -o conftest.o -c conftest.$suffix 2>&1)"
    if [[ $? -ne 0 ]]; then
        echo "could not run $1 to extract verbose output"
        return 1
    fi
    # The greediness of '.*' ensures we find the last match
    if [[ $output =~ (.*[ \'\"=]$option=([^ \'\"]+)) ]]; then
        match="${BASH_REMATCH[2]}"
        if [[ $match =~ ^/ ]]; then
            # absolute path
            result="$match"
        elif [[ $match =~ / ]]; then
            # relative path
            result=$(cd $(dirname "$match") && pwd -P)/$(basename "$match")
        else
            # bare name to search in $PATH
            result=$(type -p $match)
        fi
        if [[ ! $result =~ ^/ || ! -x "$result" ]]; then
            echo "failed to convert '$match' to an absolute path to a compiler executable"
            return 1
        fi
    else
        result="$(type -p $exe 2>/dev/null)"
        if [[ $? -ne 0 ]]; then
            echo "failed to locate $exe in \$PATH"
            return 1
        fi
    fi
    local icpc_gnu_version=$gnu_version
    check_gnu_version CXX "$result"
    if [[ $? -gt 1 ]]; then
        echo "could not run $result to extract GNU version"
        return 1
    elif [[ $icpc_gnu_version != $gnu_version ]]; then
        local msg="$result did not report expected version $icpc_gnu_version (got $gnu_version)"
        # Ick!  Older icpc seen to sometimes misreport GNUC info for /usr/bin/{gcc,g++}
        # So, error-out only if not testing the system compiler or the major versions dont't match
        if [[ $result != "/usr/bin/$exe" || ${icpc_gnu_version%%.*} != ${gnu_version%%.*} ]]; then
            echo "$msg"
            return 1
        else
            echo -e "WARNING: $msg\n" >&2
        fi
    fi
    case $1 in
         CC) echo "-gcc-name=$result";;
        CXX) # Ick!  Need both -gcc-name and -gxx-name to be completely effective on some systems.
             # Note we do not xform g++ to gcc in $result, since icpc doesn't seem to care.
             # That is a good thing, since such a transformation would be fragile.
             echo "-gxx-name=$result -gcc-name=$result";;
    esac
}

# checks specific to Intel compilers:
check_intel_compiler() {
    check_gnu_version CXX
    case $? in
        0)  # OK
            ;;
        1)  # Too low
            echo "ERROR: UPC++ with Intel compilers requires use of g++ version $MIN_GNU_STRING or" \
                 "newer, but version $gnu_version was detected."
            echo 'Please do `module load gcc`, or otherwise ensure a new-enough g++ is used by the' \
                 'Intel C++ compiler.'
            if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
                echo 'An explicit `-gxx-name=...` and/or `-gcc-name=...` option in the value of' \
                     '$CXX or $CXXFLAGS may be necessary.  Information on these options is available' \
                     'from Intel'\''s \ documentation, such as `man icpc`.'
            fi
            return 1
            ;;
        *)  # Probe failed
            return 1
            ;;
    esac

    # Find the actual g++ in use
    # Append to CXXFLAGS unless already present there, or in CXX
    local gxx_name # do not merge w/ assignment or $? is lost!
    gxx_name=$(get_intel_name_option CXX)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: $gxx_name"
        if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
          echo 'Unable to determine the g++ in use by $CXX.  An explicit `-gxx-name=...` and/or' \
               '`-gcc-name=...` option in the value of $CXX or $CXXFLAGS may be necessary.  Information'\
               'on these options is available from Intel'\''s documentation, such as `man icpc`.'
        fi
        return 1
    fi
    if [[ -n $gxx_name && ! "$CXX $CXXFLAGS " =~ " $gxx_name " ]]; then
        CXXFLAGS+="${CXXFLAGS+ }$gxx_name"
    fi

    # same for C compiler, allowing (gasp) that it might be different
    # note that no floor is imposed ($? = 0,1 both considered success)
    check_gnu_version CC
    if [[ $? -gt 1 ]]; then
        return 1   # error was already printed
    fi
    local gcc_name # do not merge w/ assignment or $? is lost!
    gcc_name=$(get_intel_name_option CC)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: $gcc_name"
        if [[ ! -d /opt/cray ]]; then # not assured of trustworthy intel environment module(s)
            echo 'Unable to determine the gcc in use by $CC.  An explicit `-gcc-name=...` option' \
                 'in the value of $CC or $CFLAGS may be necessary.  Information on this option is' \
                 'available from Intel'\''s documentation, such as `man icc`.'
        fi
        return 1
    fi
    if [[ -n $gcc_name && ! "$CC $CFLAGS " =~ " $gcc_name " ]]; then
        CFLAGS+="${CFLAGS+ }$gcc_name"
    fi
}

# platform_sanity_checks(): defaults $CC and $CXX if they are unset
#   validates the compiler and system versions for compatibility
#   setting UPCXX_INSTALL_NOCHECK=1 disables this function *completely*.
#   That includes disabling many side-effects such as platform-specific
#   defaults, conversion of CC and CXX to full paths, and addition of
#   certain options to {C,CXX}FLAGS.
platform_sanity_checks() {
    if test -z "$UPCXX_INSTALL_NOCHECK" ; then
        local KERNEL=`uname -s 2> /dev/null`
        local KERNEL_GOOD=
        if test Linux = "$KERNEL" || test Darwin = "$KERNEL" ; then
            KERNEL_GOOD=1
        fi
        if [[ $UPCXX_CROSS =~ ^cray-aries- ]]; then
            if test -n "$CRAY_PRGENVCRAY" && expr "$CRAY_CC_VERSION" : "^[78]" > /dev/null; then
                echo 'ERROR: UPC++ on Cray XC with PrgEnv-cray requires cce/9.0 or newer.'
                exit 1
            elif test -n "$CRAY_PRGENVCRAY" && expr x"$CRAY_PE_CCE_VARIANT" : "xCC=Classic" > /dev/null; then
                echo 'ERROR: UPC++ on Cray XC with PrgEnv-cray does not support the "-classic" compilers such as' \
                     $(grep -o 'cce/[^:]*' <<<$LOADEDMODULES)
                exit 1
            elif test -z "$CRAY_PRGENVGNU$CRAY_PRGENVINTEL$CRAY_PRGENVCRAY"; then
                echo 'WARNING: Unsupported PrgEnv.' \
                     'UPC++ on Cray XC currently supports PrgEnv-gnu, intel or cray. ' \
                     'Please do: `module switch PrgEnv-[CURRENT] PrgEnv-[FAMILY]`' \
                     'for your preferred compiler FAMILY.'
                # currently neither GOOD nor BAD
            fi
            CC=${CC:-cc}
            CXX=${CXX:-CC}
            MIN_GNU_MAJOR=7
            if test -z "$CRAY_PRGENVINTEL"; then
              MIN_GNU_MINOR=1
            else
              # Old icpc with new g++ reports 0 for __GNUC_MINOR__.
              # Since GCC releases START at MINOR=1 (never 0) we can safely
              # allow a MINOR==0 knowing the real value is at least 1.
              MIN_GNU_MINOR=0
            fi
            MIN_GNU_PATCH=0
            MIN_GNU_STRING='7.1'
        elif test "$KERNEL" = "Darwin" ; then # default to XCode clang
            CC=${CC:-/usr/bin/clang}
            CXX=${CXX:-/usr/bin/clang++}
        else
            CC=${CC:-gcc}
            CXX=${CXX:-g++}
        fi
        local ARCH=`uname -m 2> /dev/null`
        local ARCH_GOOD=
        local ARCH_BAD=
        if test x86_64 = "$ARCH" ; then
            ARCH_GOOD=1
        elif test ppc64le = "$ARCH" ; then
            ARCH_GOOD=1
        elif test aarch64 = "$ARCH" ; then
            ARCH_GOOD=1
            # ARM-based Cray XC not yet tested
            if test -n "$CRAY_PEVERSION" ; then
              ARCH_GOOD=
            fi
        elif expr "$ARCH" : 'i.86' >/dev/null 2>&1 ; then
            ARCH_BAD=1
        fi

        # absify compilers, checking they exist
        cxx_exec=$(check_tool_path "$CXX")
        if [[ $? -ne 0 ]]; then
            echo "ERROR: CXX='${CXX%% *}' $cxx_exec"
            exit 1
        fi
        CXX=$cxx_exec
        cc_exec=$(check_tool_path "$CC")
        if [[ $? -ne 0 ]]; then
            echo "ERROR: CC='${CC%% *}' $cc_exec"
            exit 1
        fi
        CC=$cc_exec
        if test -z "$UPCXX_INSTALL_QUIET" ; then
            echo $CXX
            $CXX --version 2>&1 | grep -v 'warning #10315'
            echo $CC
            $CC --version 2>&1 | grep -v 'warning #10315'
            echo " "
        fi

        local CXXVERS=`$CXX --version 2>&1`
        local CCVERS=`$CC --version 2>&1`
        local COMPILER_BAD=
        local COMPILER_GOOD=
        local EXTRA_RECOMMEND=
        if echo "$CXXVERS" | egrep 'Apple LLVM version [1-7]\.' 2>&1 > /dev/null ; then
            COMPILER_BAD=1
        elif echo "$CXXVERS" | egrep 'Apple LLVM version ([8-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
        elif echo "$CXXVERS" | egrep 'PGI Compilers and Tools'  > /dev/null ; then
            if [[ $UPCXX_CROSS =~ ^cray-aries- ]]; then
               : # PrgEnv-pgi: currently neither GOOD nor BAD
            elif egrep ' +(20\.[789]|20\.1[0-2]|2[1-9]\.[0-9]+)-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgc++ (aka nvc++) 20.7-0 LLVM 64-bit target on x86-64 Linux -tp nehalem"
               # 20.7 and up are known BAD
               # TODO: update with end range before 2030
               COMPILER_BAD=1
               EXTRA_RECOMMEND='
       As an exception to the above, PGI (aka NVIDIA HPC SDK) 20.7 and newer are NOT currently supported.'
            elif [[ "$ARCH,$KERNEL" = 'x86_64,Linux' ]] &&
                 egrep ' +(19|[2-9][0-9])\.[0-9]+-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgc++ 19.7-0 LLVM 64-bit target on x86-64 Linux -tp nehalem"
               # 19.1 and newer "GOOD"
               COMPILER_GOOD=1
            elif [[ "$ARCH,$KERNEL" = 'ppc64le,Linux' ]] &&
                 egrep ' +(18\.10|(19|[2-9][0-9])\.[0-9]+)-' <<<"$CXXVERS" 2>&1 >/dev/null ; then
               # Ex: "pgc++ 18.10-0 linuxpower target on Linuxpower"
               # 18.10 and newer "GOOD" (no 18.x was released for x > 10)
               COMPILER_GOOD=1
            else
               # Unsuported platform or version
               COMPILER_BAD=1
            fi
        elif echo "$CXXVERS" | egrep 'IBM XL'  > /dev/null ; then
            COMPILER_BAD=1
        elif echo "$CXXVERS" | egrep 'Free Software Foundation' 2>&1 > /dev/null &&
             ! check_gnu_version CXX &> /dev/null; then
            COMPILER_BAD=1
        elif test -z "$CRAY_PRGENVINTEL" && \
             echo "$CXXVERS" | egrep ' +\(ICC\) +(17\.0\.[2-9]|1[89]\.|2[0-9]\.)' 2>&1 > /dev/null ; then
	    # Ex: icpc (ICC) 18.0.1 20171018
            check_intel_compiler || exit 1
            COMPILER_GOOD=1
        elif test -n "$CRAY_PRGENVINTEL" && \
             echo "$CXXVERS" | egrep ' +\(ICC\) +(18\.0\.[1-9]|19\.|2[0-9]\.)' 2>&1 > /dev/null ; then
            check_intel_compiler || exit 1
            COMPILER_GOOD=1
        elif echo "$CXXVERS" | egrep ' +\(ICC\) ' 2>&1 > /dev/null ; then
            check_intel_compiler
            if [[ $? -ne 0 ]]; then
              if [[ -d /opt/cray ]]; then
                echo 'ERROR: Your Intel compiler is too old, please `module swap intel intel` (or' \
                     'similar) to load a supported version'
                exit 1
              else
                # continue past messages for a too-old libstdc++ and proceed to
                # warning about unsupported compiler, with a line break between
                echo
              fi
            fi
        elif echo "$CXXVERS" | egrep 'Free Software Foundation' 2>&1 > /dev/null &&
             check_gnu_version CXX &> /dev/null; then
            COMPILER_GOOD=1
            # Arm Ltd's gcc not yet tested
            if test aarch64 = "$ARCH" && echo "$CXXVERS" | head -1 | egrep ' +\(ARM' 2>&1 > /dev/null ; then
              COMPILER_GOOD=
            fi
        elif echo "$CXXVERS" | egrep 'clang version [23]' 2>&1 > /dev/null ; then
            COMPILER_BAD=1
        elif test x86_64 = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([4-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
        elif test ppc64le = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([5-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
	    # Issue #236: ppc64le/clang support floor is 5.x. clang-4.x/ppc has correctness issues and is deliberately left "unvalidated"
            COMPILER_GOOD=1
        elif test aarch64 = "$ARCH" && echo "$CXXVERS" | egrep 'clang version ([4-9]\.|[1-9][0-9])' 2>&1 > /dev/null ; then
            COMPILER_GOOD=1
            # Arm Ltd's clang not yet tested
            if echo "$CXXVERS" | egrep '^Arm C' 2>&1 > /dev/null ; then
              COMPILER_GOOD=
            fi
        fi

        local RECOMMEND
        read -r -d '' RECOMMEND<<'EOF'
We recommend one of the following C++ compilers (or any later versions):
           Linux on x86_64:   g++ 6.4.0, LLVM/clang 4.0.0, PGI 19.1, Intel C 17.0.2
           Linux on ppc64le:  g++ 6.4.0, LLVM/clang 5.0.0, PGI 18.10
           Linux on aarch64:  g++ 6.4.0, LLVM/clang 4.0.0
           macOS on x86_64:   g++ 6.4.0, Xcode/clang 8.0.0
           Cray XC systems:   PrgEnv-gnu with gcc/7.1.0 environment module loaded
                              PrgEnv-intel with Intel C 18.0.1 and gcc/7.1.0 environment modules loaded
                              PrgEnv-cray with cce/9.0.0 environment module loaded
                              ALCF's PrgEnv-llvm/4.0
EOF
        if test -n "$ARCH_BAD" ; then
            echo "ERROR: This version of UPC++ does not support the '$ARCH' architecture."
            echo "ERROR: $RECOMMEND$EXTRA_RECOMMEND"
            exit 1
        elif test -n "$COMPILER_BAD" ; then
            echo 'ERROR: Your C++ compiler is known to lack the support needed to build UPC++. '\
                 'Please set $CC and $CXX to point to a newer C/C++ compiler suite.'
            echo "ERROR: $RECOMMEND$EXTRA_RECOMMEND"
            exit 1
        elif test -z "$COMPILER_GOOD" || test -z "$KERNEL_GOOD" || test -z "$ARCH_GOOD" ; then
            echo 'WARNING: Your C++ compiler or platform has not been validated to run UPC++'
            echo "WARNING: $RECOMMEND$EXTRA_RECOMMEND"
        fi
    fi
    return 0
}

platform_settings() {
   local KERNEL=`uname -s 2> /dev/null`
   case "$KERNEL" in
     *)
       ;;
   esac
}

