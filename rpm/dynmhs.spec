Name: dynmhs
Version: 0.0.0~alpha0
Release: 1
Summary: Dynamic Multi-Homing Setup (DynMHS)
Group: Applications/Internet
License: GPL-3.0-or-later
URL: https://www.nntb.no/~dreibh/dynmhs/
Source: https://www.nntb.no/~dreibh/dynmhs/download/%{name}-%{version}.tar.xz

AutoReqProv: on
BuildRequires: boost-devel
BuildRequires: cmake
BuildRequires: gcc
BuildRequires: gcc-c++
BuildRoot: %{_tmppath}/%{name}-%{version}-build

Requires: iproute
Recommends: hipercontracer
Recommends: netperfmeter
Recommends: subnetcalc
Suggests: td-system-info


%description
DynMHS dynamically sets up IP routing rules, to allow using multiple
network connections simultaneously. That is, for each relevant network
interface, a separate routing table is created and maintained. For each
source address of a network interface managed by DynMHS, routing rules
are maintained to point to the corresponding routing table. Software
binding to a specific interface address can then use a specific
network. In addition, multi-homing-capable network protocols like the
Multi-Path TCP (MPTCP) or the Stream Control Transmission Protocol (SCTP)
can take advantage of multi-homing for redundancy and load balancing.

%prep
%setup -q

%build
%cmake -DCMAKE_INSTALL_PREFIX=/usr .
%cmake_build

%install
%cmake_install

%files
%{_bindir}/dynmhs
%{_datadir}/bash-completion/completions/dynmhs
%{_mandir}/man1/dynmhs.1.gz
%{_sysconfdir}/dynmhs/dynmhs.conf
/lib/systemd/system/dynmhs.service


%doc

%changelog
* Fri Nov 08 2024 Thomas Dreibholz <dreibh@simula.no> - 0.0.0
- Created RPM package.
