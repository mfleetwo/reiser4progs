%define enable_debug 1

%define _sbindir /sbin

Name: reiser4progs
Version: 0.4.13
Release: 1
Summary: Utilities for reiser4 filesystems
License: GPL
Group: System Environment/Base
URL: http://www.namesys.com/
Source: reiser4progs-%{version}.tar.gz
BuildRequires: libaal-devel
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description
Utilities for manipulating reiser4 filesystems.

%package devel
Summary: Development libraries and headers for developing reiser4 tools.
Group: Development/Libraries

%description devel
Development libraries and headers for developing reiser4 tools.

%prep
%setup -q

%build
%configure \
%if %{enable_debug}
        --enable-debug \
%else
        --disable-debug \
%endif
        --enable-stand-alone \
        --disable-plugins-check \
        --disable-fnv1-hash \
        --disable-rupasov-hash \
        --disable-tea-hash \
        --disable-deg-hash
make

%install
rm -rf $RPM_BUILD_ROOT
%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files
%defattr(-,root,root)
%doc AUTHORS BUGS COPYING CREDITS INSTALL NEWS README THANKS TODO
%{_libdir}/libreiser4-0.4.so.*
%{_libdir}/libreiser4-alone-0.4.so.*
%{_libdir}/librepair-0.4.so.*
%{_sbindir}/cpfs.reiser4
%{_sbindir}/debugfs.reiser4
%{_sbindir}/fsck.reiser4
%{_sbindir}/make_reiser4
%{_sbindir}/measurefs.reiser4
%{_sbindir}/mkfs.reiser4
%{_sbindir}/resizefs.reiser4
%{_mandir}/man8/*.gz

%files devel
%{_includedir}/aux/*.h
%dir %{_includedir}/reiser4
%{_includedir}/reiser4/*.h
%{_includedir}/repair/*.h
%{_datadir}/aclocal/libreiser4.m4
%{_libdir}/libreiser4.*a
%{_libdir}/libreiser4-alone.*a
%{_libdir}/librepair.*a

%changelog
* Fri Aug 29 2003 Yuey V Umanets <umka@namesys.com>
- Some cleanups and improvements inf this spec file
* Wed Aug 27 2003 David T Hollis <dhollis@davehollis.com>
- RPM package created