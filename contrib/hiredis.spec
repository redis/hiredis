Summary: hiredis
Name: hiredis
Version: 0.10.1
Release: 1
License: Copyright (c) 2009-2011, Salvatore Sanfilippo <antirez at gmail dot com>, Pieter Noordhuis <pcnoordhuis at gmail dot com>
Group: Develepoment/Library
URL: https://github.com/antirez/hiredis.git
Source0: hiredis-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Provides: libev
Provides: libevent

%description 
Hiredis is a minimalistic C client library for the Redis database.

It is minimalistic because it just adds minimal support for the protocol, but at the same time it uses an high level printf-alike API in order to make it much higher level than otherwise suggested by its minimal code base and the lack of explicit bindings for every Redis command.

Apart from supporting sending commands and receiving replies, it comes with a reply parser that is decoupled from the I/O layer. It is a stream parser designed for easy reusability, which can for instance be used in higher level language bindings for efficient reply parsing.

Hiredis only supports the binary-safe Redis protocol, so you can use it with any Redis version >= 1.2.0.

The library comes with multiple APIs. There is the synchronous API, the asynchronous API and the reply parsing API.

%package devel
Summary: Redis C binding library
Group: System/Libraries

%description devel
Redis C client library for communicating with Redis Server.


%files devel
%defattr(-, root, root, 0755)
%{_includedir}/*
%{_libdir}/lib*

%prep
%setup

%build
%{__make}

%install
%{__rm} -rf %{buildroot}

#mkdir -p /usr/local/include/hiredis /usr/local/lib
%{__mkdir} -p %{buildroot}%{_includedir}/hiredis %{buildroot}%{_includedir}/hiredis/adapters %{buildroot}%{_libdir}

#cp -a hiredis.h async.h adapters /usr/local/include/hiredis
%{__install} -Dp -m 0755 hiredis.h %{buildroot}%{_includedir}/hiredis
%{__install} -Dp -m 0755 async.h %{buildroot}%{_includedir}/hiredis
%{__install} -Dp -m 0755 adapters/* %{buildroot}%{_includedir}/hiredis/adapters

#cd /usr/local/lib && ln -sf libhiredis.so.0.10 libhiredis.so.0
#cd /usr/local/lib && ln -sf libhiredis.so.0 libhiredis.so
%{__install} -Dp -m 0755 libhiredis.so %{buildroot}%{_libdir}
#cp -a libhiredis.a /usr/local/lib
%{__install} -Dp -m 0755 libhiredis.a %{buildroot}%{_libdir}

cd %{buildroot}%{_libdir}
%{__mv} libhiredis.so libhiredis.so.%{version}
ln -sf libhiredis.so.0.%{version} libhiredis.so.0
ln -sf libhiredis.so.0.%{version} libhiredis.so
cd -

%clean
%{__rm} -rf %{buildroot}

%changelog
* Fri Jan 13 2012 thinker0 <thinker0 ant gmail dot com> - 0.10.1
- Initial hiredis.spec
