dnl $Id: acinclude.m4,v 1.2 2003/04/25 07:22:47 joshk Exp $

AC_DEFUN([AC_SUBST_DIR], [
	ifelse($2,,,$1="[$]$2")
	$1=`(
		test "x$prefix" = xNONE && prefix="$ac_default_prefix"
		test "x$exec_prefix" = xNONE && exec_prefix="${prefix}"
		eval echo \""[$]$1"\"
	)`
	AC_SUBST($1)
])

