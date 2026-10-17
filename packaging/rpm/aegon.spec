Name:           aegon
Version:        0.1.0~alpha2
Release:        1%{?dist}
Summary:        High-Performance C++23 Asynchronous Web & Data Framework
License:        MIT
URL:            https://github.com/UdayKhare09/Aegon
Source0:        https://github.com/UdayKhare09/Aegon/archive/refs/tags/v0.1.0-alpha2.tar.gz

BuildRequires:  cmake >= 3.20
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  git
BuildRequires:  openssl-devel
BuildRequires:  sqlite-devel
BuildRequires:  libpq-devel
BuildRequires:  libnghttp2-devel
BuildRequires:  libngtcp2-devel
BuildRequires:  libnghttp3-devel
BuildRequires:  libdeflate-devel
BuildRequires:  liburing-devel

Requires:       openssl-libs
Requires:       sqlite-libs
Requires:       libpq
Requires:       libnghttp2
Requires:       libngtcp2
Requires:       libnghttp3
Requires:       libdeflate
Requires:       liburing

%description
Aegon is a C++23 async web framework with HTTP/2, HTTP/3, Redis,
ORM, and memory store support. Built on io_uring (Linux) / IOCP (Windows).
Glaze is fetched automatically at build time.

%prep
%autosetup -n Aegon-0.1.0-alpha2

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DAEGON_BUILD_TESTS=OFF \
    -DAEGON_BUILD_SAMPLES=OFF \
    -DAEGON_BUILD_BENCHMARKS=OFF
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%{_libdir}/libaegon*.a
%{_libdir}/cmake/Aegon/
%{_includedir}/aegon/

%changelog
* Sat Oct 17 2026 Uday Khare <udaykhare77@gmail.com> - 0.1.0~alpha2-1
- Initial RPM packaging
