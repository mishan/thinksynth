dnl $Id: acinclude.m4,v 1.3 2003/05/03 03:53:49 joshk Exp $

AC_DEFUN([AC_SUBST_DIR], [
	ifelse($2,,,$1="[$]$2")
	$1=`(
		test "x$prefix" = xNONE && prefix="$ac_default_prefix"
		test "x$exec_prefix" = xNONE && exec_prefix="${prefix}"
		eval echo \""[$]$1"\"
	)`
	AC_SUBST($1)
])

AC_DEFUN([AC_CXX_MT_BROKEN], [
	AC_MSG_CHECKING([if using -MT breaks $CXX])
	echo 'int test_func (void) {}' > conftest.cpp
	$CXX -Wp,-MMD,.confdep,-MTconftest.so -shared conftest.cpp -o conftest.so

	if test -f conftest.so -a `nm conftest.so | grep test_func | wc -l` -ne 0; then
		CXX_MT_BROKEN=no
	else
		CXX_MT_BROKEN=yes
	fi

	AC_MSG_RESULT($CXX_MT_BROKEN)
	AC_SUBST(CXX_MT_BROKEN)

	rm -f conftest.cpp conftest.so .confdep
])
