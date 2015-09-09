AC_INIT([leaf], [0.1], [baloo@gandi.net])
AM_INIT_AUTOMAKE([-Wall -W foreign subdir-objects])
AC_PROG_CC
AC_PROG_CC_STDC
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([
 Makefile
 src/Makefile
])

PKG_CHECK_MODULES([LIBBSD], [libbsd-overlay])
PKG_CHECK_MODULES([LIBLLDP], [lldpctl])
PKG_CHECK_MODULES([LIBBL], [libnl-3.0])
PKG_CHECK_MODULES([LIBNL_GENL], [libnl-genl-3.0])
PKG_CHECK_MODULES([LIBNL_ROUTE], [libnl-route-3.0])

###############################################################################
# If using GCC, set some additional AM_CFLAGS:
###############################################################################

if test "$GCC" = yes ; then
        echo
        echo "GCC extensions:"
fi

# Always do the visibility check but don't set AM_CFLAGS on Windows.
# This way things get set properly even on Windows.
gl_VISIBILITY
if test -n "$CFLAG_VISIBILITY" && test "$is_w32" = no; then
        AM_CFLAGS="$AM_CFLAGS $CFLAG_VISIBILITY"
fi

if test "$GCC" = yes ; then
        # Enable as much warnings as possible. These commented warnings won't
        # work for this package though:
        #   * -Wunreachable-code breaks several assert(0) cases, which are
        #     backed up with "return LZMA_PROG_ERROR".
        #   * -Wcast-qual would break various things where we need a non-const
        #     pointer although we don't modify anything through it.
        #   * -Wcast-align breaks optimized CRC32 and CRC64 implementation
        #     on some architectures (not on x86), where this warning is bogus,
        #     because we take care of correct alignment.
        #   * -Winline, -Wdisabled-optimization, -Wunsafe-loop-optimizations
        #     don't seem so useful here; at least the last one gives some
        #     warnings which are not bugs.
        for NEW_FLAG in \
                        -Wall \
                        -Wextra \
                        -Wformat=2 \
                        -Winit-self \
                        -Wmissing-include-dirs \
                        -Wstrict-aliasing \
                        -Wfloat-equal \
                        -Wundef \
                        -Wshadow \
                        -Wpointer-arith \
                        -Wbad-function-cast \
                        -Wwrite-strings \
                        -Wlogical-op \
                        -Waggregate-return \
                        -Wstrict-prototypes \
                        -Wold-style-definition \
                        -Wmissing-prototypes \
                        -Wmissing-declarations \
                        -Wmissing-noreturn \
                        -pedantic \
                        -Wredundant-decls
        do
                AC_MSG_CHECKING([if $CC accepts $NEW_FLAG])
                OLD_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS -Werror $NEW_FLAG"
                dnl Correctly updated code
                AC_LINK_IFELSE([AC_LANG_SOURCE([int main() { return 0; }])],
                  [
                   AM_CFLAGS="$AM_CFLAGS $NEW_FLAG"
                   AC_MSG_RESULT([yes])
                   ], [
                       AC_MSG_RESULT([no])
                       ])
                CFLAGS="$OLD_CFLAGS"
        done

        AC_ARG_ENABLE([werror],
                AC_HELP_STRING([--enable-werror], [Enable -Werror to abort
                        compilation on all compiler warnings.]),
                [], [enable_werror=yes])
        if test "x$enable_werror" = "xyes"; then
                AM_CFLAGS="$AM_CFLAGS -Werror"
        fi
fi

AC_CHECK_PROGS(
    [CLANG_FORMAT],
    [clang-format-3.7 clang-format-3.6 clang-format],
    [no]
)

AM_CFLAGS="$AM_CFLAGS $WARNINGCFLAGS"

CFLAGS="-g -O2"

# Add default AM_CFLAGS.
AC_SUBST([AM_CFLAGS])

AC_OUTPUT
