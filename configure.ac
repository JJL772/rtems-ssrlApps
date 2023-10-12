AC_PREREQ(2.62)

m4_include(./m4/cvstag.m4)

AC_INIT(ssrlApps, TILLAC_CVSTAG([$Name$]), <strauman@slac.stanford.edu>)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign 1.10])
AM_MAINTAINER_MODE

AH_TEMPLATE([NFS_SUPPORT])
AH_TEMPLATE([HAVE_LIBBSD])

# Must add this BEFORE TILLAC_RTEMS_SETUP
# so that the dummy-top 'config.status' also
# knows how to make a config.h
AM_CONFIG_HEADER(config.h)

# Let this configure and sub-configures search
# the temporary installdir for includes and libraries...
AC_MSG_NOTICE([TILLAC MAKEVARS: >${RTEMS_TILL_MAKEVARS_SET}<])
if test "xx${RTEMS_TILL_MAKEVARS_SET}" = xxYES ; then
CPPFLAGS="$CPPFLAGS -I`pwd`/data/include"
LDFLAGS="$LDFLAGS -L`pwd`/data/lib"
fi
TILLAC_RTEMS_LDFLAGS="-L`pwd`/"'$RTEMS_BSP/data/lib'
TILLAC_RTEMS_CPPFLAGS="-I`pwd`/"'$RTEMS_BSP/data/include'

AC_SUBST([enable_subdirs])
AC_SUBST([all_subdirs])
AC_SUBST([rtems_cpu],[$host_cpu])
AC_SUBST([rtems_bsp],[$enable_rtemsbsp])

# Check for critical programs we need for building
TILLAC_RTEMS_CHECK_TOOLS

TILLAC_RTEMS_SETUP

dnl This doesn't work -- somehow automake gets confused
dnl (perhaps because we have both, a Makefile and a makefile):
dnl automake doesn't produce a Makefile.in. However, if
dnl we add AC_CONFIG_FILES([Makefile]) then it works...
dnl
dnl my_config_files=Makefile
dnl
AC_CONFIG_FILES([Makefile])
enable_subdirs=
all_subdirs=

TILLAC_M4_IF_PRESENT([libbspExt],
[if test -d $srcdir/libbspExt ; then
	have_bspext=yes
	AC_CONFIG_FILES([libbspExt/Makefile])
	all_subdirs="$all_subdirs libbspExt"
	case ${host_cpu} in
		powerpc | i386)
			enable_subdirs="${enable_subdirs} libbspExt"
			AC_DEFINE(HAVE_LIBBSPEXT,1,[Whether we have the libbspExt extension library])
		;;
		*)
		;;
	esac
fi]dnl
)

if test -d $srcdir/cexp ; then
	all_subdirs="$all_subdirs cexp"
	enable_subdirs="$enable_subdirs cexp"
	AC_CONFIG_SUBDIRS([cexp])
	AC_DEFINE(HAVE_CEXP,1,[Whether CEXP is installed])
	have_cexp=yes
fi

#Maybe libbspExt is already installed?
if test ! "${have_bspext}" = "yes" ; then
	AC_CHECK_LIB([bspExt],[bspExtInit],
		[AC_MSG_NOTICE([Using existing libbspExt])
		 have_bspext=yes
		],
		,
		TILLAC_RTEMS_CHECK_LIB_ARGS)
fi

TILLAC_M4_IF_PRESENT([rtems-gdb-stub],
[if test -d $srcdir/rtems-gdb-stub; then
	AC_CONFIG_FILES([rtems-gdb-stub/Makefile])
	all_subdirs="$all_subdirs rtems-gdb-stub"
	case ${host_cpu} in
		powerpc | i386 | m68k)
			enable_subdirs="${enable_subdirs} rtems-gdb-stub"
		;;
		*)
		;;
	esac
fi]dnl
)

# Check for PCI support
AC_MSG_NOTICE([Checking for PCI support of your BSP])
# No use to use a 'CHECKING' message; AC_CHECK_FUNC already reports...
AC_CHECK_FUNC(pci_find_device)
AC_CHECK_FUNC(_CPU_Initialize_altivec)

# BSP_commandline_string is not a function but
# the macro just checks if it can link the symbol
# which is good enough
AC_CHECK_FUNCS([BSP_commandline_string])
AC_CHECK_DECL([BSP_commandline_string],
	[AC_DEFINE([DECL_BSP_COMMANDLINE_STRING],1,[Whether BSP_commandline_string is declared in <bsp.h>])],,
	[#include <rtems.h>
	#include <bsp.h>])
AC_CHECK_FUNCS([bsp_cmdline])

TILLAC_M4_IF_PRESENT([telnetd],
[if test -d $srcdir/telnetd ; then
	AC_CONFIG_FILES([telnetd/Makefile])
	all_subdirs="${all_subdirs} telnetd"
	enable_subdirs="${enable_subdirs} telnetd"
	AC_MSG_CHECKING([if bundled rtems_telnetd_initialize() takes 6 args])
	AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <rtems.h>
	#include <rtems/telnetd.h>]],
	[[rtems_telnetd_initialize(0,0,0,0,0,0);]])],
	[AC_MSG_RESULT([Yes -- old style rtems_telnetd_initialize w/o config table])
	 have_bundled_telnetd=oldstyle
	],
	[AC_MSG_RESULT([No])
	AC_MSG_CHECKING([if bundled rtems_telnetd_initialize uses a config table])
	AC_CHECK_TYPE([rtems_telnetd_config_table],
		[AC_MSG_RESULT([new style rtems_telnetd_initialize with config table])
		 have_bundled_telnetd=newstyle
		],
		[
		AC_MSG_RESULT([No])
		],
		[#include <rtems.h>
		 #include <rtems/telnetd.h>
		])
	])	
	# We used to do 'AC_CHECK_LIB([telnetd])' here but that fails
	# for 4.10 because the config table is missing. Just omit
	# this paranoia test for now...
	if ! test "${have_bundled_telnetd+set}" = "set" ; then
		have_bundled_telnetd=no
	fi
	case "${have_bundled_telnetd}" in
		oldstyle)
			AC_MSG_NOTICE([Using bundled telnetd (oldstyle)])
			AC_DEFINE([HAVE_BUNDLED_TELNETD],
					  [1],
					  [Defined to 1/2 if you have bundled telnetd without/with config table])
		;;
		newstyle)
			AC_MSG_NOTICE([Using bundled telnetd (newstyle)])
			AC_DEFINE([HAVE_BUNDLED_TELNETD],
					  [2],
					  [Defined to 1/2 if you have bundled telnetd without/with config table])
		;;
		*)
			AC_MSG_NOTICE([Using unbundled telnetd])
			have_bundled_telnetd=no
		;;
		esac
		if test ! xx"${have_bundled_telnetd}" = xx"no" ; then
			have_bundled_telnetd=yes
		fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([miscUtils],
[if test -d $srcdir/miscUtils ; then
	AC_CONFIG_FILES([miscUtils/Makefile])
	enable_subdirs="${enable_subdirs} miscUtils"
	all_subdirs="${all_subdirs} miscUtils"
fi]dnl
)

TILLAC_M4_IF_PRESENT([monitor],
[if test -d $srcdir/monitor ; then
	AC_CONFIG_FILES([monitor/Makefile])
	all_subdirs="${all_subdirs} monitor"
	if test "${enable_tecla}" = "no" ; then
		AC_MSG_NOTICE(['monitor' package disabled because you disabled TECLA -- the monitor would depend on it])
	else
		enable_subdirs="${enable_subdirs} monitor"
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([ntpNanoclock],
[if test -d $srcdir/ntpNanoclock ; then
	AC_CONFIG_FILES([ntpNanoclock/Makefile])
	all_subdirs="${all_subdirs} ntpNanoclock"
	enable_subdirs="${enable_subdirs} ntpNanoclock"
	if case $enable_rtemsbsp in uC5282 ) true ;; *) false;; esac ; then
		AC_MSG_CHECKING([Checking if PR1738 has been fixed])
		AC_CHECK_DECL([__SRAMBASE.pitc_per_tick],
           [AC_DEFINE([DECL_SRAM_PITC_PER_TICK],1,[Whether bsp.h declares pitc_per_tick in SRAM (PR1738)])],,[#include <rtems.h>
                              #include <bsp.h>])
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([svgmWatchdog],
[if test -d $srcdir/svgmWatchdog ; then
	AC_CONFIG_FILES([svgmWatchdog/Makefile])
	all_subdirs="${all_subdirs} svgmWatchdog"
	# Do we have BSP support for our watchdog
	AC_MSG_CHECKING([Looking for BSP support for the watchdog])
	if test -f $srcdir/svgmWatchdog/bsp_${enable_rtemsbsp}.c ; then
		AC_MSG_RESULT([OK, found BSP support])
		WATCHDOG_BSP_SUPPORT=bsp_${enable_rtemsbsp}
	# No BSP support - is this a known BSP with a E500 CPU ?
	elif case $enable_rtemsbsp in mvme3100 ) true ;; *) false;; esac ; then
		AC_MSG_RESULT([OK, using generic PPC-E500 support])
		WATCHDOG_BSP_SUPPORT=bsp_booke
	# No; if it is a i386 board then we hope it has SMIC
	elif test "${host_cpu}" = "i386" ; then
		AC_MSG_RESULT([MAYBE, using generic SMIC support; hoping your board has SMIC])
		WATCHDOG_BSP_SUPPORT=bsp_smic
	else
		AC_MSG_RESULT([FAILED. Watchdog not supported on this platform])
	fi
	if test "${WATCHDOG_BSP_SUPPORT+set}" = "set" ; then
		enable_subdirs="${enable_subdirs} svgmWatchdog"
		AC_SUBST([WATCHDOG_BSP_SUPPORT])
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([coldfUtils],
[if test -d $srcdir/coldfUtils ; then
	AC_CONFIG_FILES([coldfUtils/Makefile])
	all_subdirs="${all_subdirs} coldfUtils"
	if test "${enable_rtemsbsp}" = "uC5282" ; then
		enable_subdirs="${enable_subdirs} coldfUtils"
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([efence],
[if test -d $srcdir/efence ; then
	AC_CONFIG_FILES([efence/Makefile])
	all_subdirs="${all_subdirs} efence"
	if test "${host_cpu}" = "powerpc" ; then
# should really check if this is a >= 604 PPC
		AC_CHECK_HEADER([libcpu/pte121.h],
		[enable_subdirs="${enable_subdirs} efence"],
		[AC_MSG_NOTICE([No <libcpu/pte121.h> found; not building libefence])]dnl
		)
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([altivec],
[if test -d $srcdir/altivec ; then
	AC_CONFIG_FILES([altivec/Makefile])
	all_subdirs="${all_subdirs} altivec"
	if test "${host_cpu}" = "powerpc" ; then
		AC_MSG_NOTICE([Checking if PPC_CACHE_ALIGNMENT is 32 bytes])
		AC_COMPILE_IFELSE(
			[AC_LANG_PROGRAM(
[[
#include <rtems.h>
#include <rtems/powerpc/powerpc.h>
#include <rtems/score/powerpc.h>
#if PPC_CACHE_ALIGNMENT != 32
#error "Altivec support assumes cache-line size of 32 bytes"
#endif
]],[[]])],
			[AC_MSG_RESULT([Ok])
			enable_subdirs="${enable_subdirs} altivec"],
			[AC_MSG_RESULT([No; not building AltiVec extension])]dnl
		)
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([bsd_eth_drivers/libbsdport],
[if test -d $srcdir/bsd_eth_drivers ; then
	AC_CONFIG_FILES([bsd_eth_drivers/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/libbsdport/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_pcn/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_le/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_em/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_fxp/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_re/Makefile])
	AC_CONFIG_FILES([bsd_eth_drivers/if_bge/Makefile])
	all_subdirs="${all_subdirs} bsd_eth_drivers"
	if test "${ac_cv_func_pci_find_device}" = "yes" ; then
		AC_CHECK_FUNC([BSP_install_rtems_shared_irq_handler])
		if test ! "${ac_cv_func_BSP_install_rtems_shared_irq_handler}" = "yes" && test ! "${have_bspext}" = "yes" ; then
			AC_MSG_NOTICE([Not building bsd_eth_drivers; BSP_install_rtems_shared_irq_handler support or libbspExt required!])
		else
			#FIXME: make these configurable options
			ENBL_82542_SUPPORT=NO
			ENBL_ICH8LAN_SUPPORT=YES
			have_emdrv=yes
			AC_SUBST([ENBL_82542_SUPPORT])
			AC_SUBST([ENBL_ICH8LAN_SUPPORT])
			enable_subdirs="${enable_subdirs} bsd_eth_drivers"
		fi
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([amdeth],
[if test -d $srcdir/amdeth ; then
	AC_CONFIG_FILES([amdeth/Makefile])
	all_subdirs="${all_subdirs} amdeth"
	if test "$ac_cv_func_pci_find_device" = "yes" ; then
		enable_subdirs="${enable_subdirs} amdeth"
		have_amddrv=yes
	else
		AC_MSG_NOTICE([Your BSP doesnt seem to have PCI; not building the amdeth driver])
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([drvLan9118],
[if test -d $srcdir/drvLan9118 ; then
	AC_CONFIG_FILES([drvLan9118/Makefile])
	all_subdirs="${all_subdirs} drvLan9118"
	case $enable_rtemsbsp in 
		uC5282)
			LANIPBASIC_SUPPORT=ln9
		;;
		beatnik)
			LANIPBASIC_SUPPORT=mve
		;;
		mvme3100)
			AC_CHECK_FUNCS([BSP_tsec_setup_1], [LANIPBASIC_SUPPORT=tsec], [LANIPBASIC_SUPPORT=])
		;;
		*)
			LANIPBASIC_SUPPORT=
		;;
	esac
	if test "$ac_cv_func_pci_find_device" = "yes" ; then
		if test "${have_emdrv+set}" = "set" ; then
			LANIPBASIC_SUPPORT="$LANIPBASIC_SUPPORT e1k"
		fi
		if test "${have_amddrv+set}" = "set" ; then
			LANIPBASIC_SUPPORT="$LANIPBASIC_SUPPORT pcn"
		fi
	fi
	if test "${LANIPBASIC_SUPPORT}XX" = "XX" ; then
		AC_MSG_NOTICE([lanIpBasic package not supported by this BSP -- not built])
	else
		enable_subdirs="${enable_subdirs} drvLan9118"
		AC_SUBST(LANIPBASIC_SUPPORT)
		AC_SUBST([LANIPBASIC_PROG],['$(LANIPBASIC_SUPPORT:%=lanIpBasic_%$(EXEEXT))'])
	fi
fi]dnl
)

TILLAC_M4_IF_PRESENT([rtemsNfs],
[if test -d $srcdir/rtemsNfs ; then
	AC_CONFIG_FILES([rtemsNfs/proto/Makefile])
	AC_CONFIG_FILES([rtemsNfs/Makefile])
	AC_CONFIG_FILES([rtemsNfs/src/Makefile])
	all_subdirs="${all_subdirs} rtemsNfs"
fi]dnl
)

# looking for bundled NFS;
build_rtems_nfs=no
AC_MSG_CHECKING([whether we should build unbundled NFS])
# Note the 5th argument; linking would fail because
# the application usually supplies rtems_bsdnet_config.
# In order to link, we create a dummy symbol.
AC_CHECK_LIB([bsd],[rtems_nfs_initialize],
	[AC_MSG_NOTICE([Using RTEMS bundled NFS])
	 AC_SUBST([NFSLIB],["-Wl,--start-group -lbsd -lrtemsbsp -lrtemscpu -Wl,--end-group"])
     nfs_enableval=2
	 have_libbsd=1
	 if test -d $srcdir/rtemsNfs ; then
	 	AC_MSG_NOTICE([Building 'dirutils' only (from unbundled NFS)])
		build_rtems_nfs=dirutils_only
	 fi],
	[if test -d $srcdir/rtemsNfs ; then
		 AC_MSG_NOTICE([Building unbundled NFS])
		 build_rtems_nfs=yes
         nfs_enableval=1
		 AC_SUBST([NFSLIB],["-lrtemsNfs -lnfsprot"])
	 fi],
	TILLAC_RTEMS_CHECK_LIB_ARGS
)
AC_DEFINE_UNQUOTED([NFS_SUPPORT],[$nfs_enableval],[Whether to use NFS (2: bundled; 1 unbundled)])
AC_DEFINE([HAVE_LIBBSD],[$have-libbsd], [Whether we are building with the new RTEMS libbsd])

AC_MSG_RESULT([Building unbundled NFS: $build_rtems_nfs])

if test ! "$build_rtems_nfs" = "no" ; then
	enable_subdirs="${enable_subdirs} rtemsNfs"
fi


TILLAC_M4_IF_PRESENT([libtecla],
[if test -d $srcdir/libtecla && test ! "${enable_tecla}" = "no" ; then
	all_subdirs="$all_subdirs libtecla"
	enable_subdirs="$enable_subdirs libtecla"
	AC_CONFIG_SUBDIRS([libtecla])
fi])

TILLAC_M4_IF_PRESENT([netboot],
[if test -d $srcdir/netboot ; then
	AC_CONFIG_FILES([netboot/Makefile])
	all_subdirs="${all_subdirs} netboot"
	case "$enable_rtemsbsp" in
		svgm|beatnik|uC5282|mvme3100)
			if test "${enable_tecla}" = "no" ; then
				AC_MSG_NOTICE(['netboot' package disabled because you disabled TECLA -- netboot would depend on it])
			else
				enable_subdirs="${enable_subdirs} netboot"
				case "$enable_rtemsbsp" in
					svgm)
						netboot_compressed=yes
					;;
					*)
					;;
				esac
			fi
	;;
		*)
	;;
	esac
fi]dnl
)

system_subdir=
TILLAC_M4_IF_PRESENT([system],
[if test -d $srcdir/system ; then
	all_subdirs="$all_subdirs system"
	case "$enable_rtemsbsp" in
		rce405)
		# real application is built somewhere else
		;;
		*)
			enable_subdirs="$enable_subdirs system"
			AC_CONFIG_FILES([ssrlApps.components])
			AC_CONFIG_SUBDIRS([system])
		;;
	esac
fi]dnl
)

AC_SUBST([system_subdir])

# an ugly hack so we can give programs that
# should run on the build host a different
# extension.
# The only way automake does not append $(EXEEXT)
# is if a PROGRAM is configure-substituted
# (see automake doc 'EXEEXT')
AC_SUBST([HOSTPROGRAM],['$(HOSTPROG:%=%$(HOSTEXEEXT))'])
AC_SUBST([installexechostbinprogs],[install-exechostbinPROGRAMS])

AM_CONDITIONAL([HAVE_NATIVE_ALTIVEC],   [test "$ac_cv_func__CPU_Initialize_altivec" = "yes"])
AM_CONDITIONAL([HAVE_CEXP],             [test "$have_cexp"             = "yes"])
AM_CONDITIONAL([BUILD_RTEMS_NFS],       [test "$build_rtems_nfs"       = "yes"])
AM_CONDITIONAL([HAVE_BUNDLED_TELNETD],  [test "$have_bundled_telnetd"  = "yes"])
AM_CONDITIONAL([NETBOOT_COMPRESSED],    [test "$netboot_compressed"    = "yes"])
AM_CONDITIONAL([ENABLE_DIAG_HOSTPROGS], [test "$enable_diag_hostprogs" = "yes"])
AM_CONDITIONAL([HAVE_LIBBSD],			[test "$have_libbsd"           = 1])
AC_OUTPUT
