Name:           weed-war
Version:        %{version}
Release:        1%{?dist}
Summary:        Lio's weed war, a screensaver from "Anathem"
License:        MIT
Source0:        weed-war-%{version}.tar.gz
BuildRequires:  gcc, make, pkgconfig(sdl2)
Requires:       swayidle

%description
Lio's weed war from Neal Stephenson's novel Anathem: Fraa Lio re-enacts the
Battle of Trantae in a meadow of clover, with three colours of starblossom for
the Sarthian cavalry and slashberry coming up behind them.

The weeds spread patch by patch; Lio's weeding steers them through the false
retreat and the pincer, and Erasmas draws it from the wall. A new war every
year, never quite the same twice.

On Wayland desktops it starts by itself after five minutes idle.
weed-war-screensaver changes the delay or turns it off.

%prep
%setup -q

%build
make %{?_smp_mflags} CFLAGS="%{optflags}" LDFLAGS="%{?__global_ldflags}"

%install
make install DESTDIR=%{buildroot} PREFIX=%{_prefix} SYSCONFDIR=%{_sysconfdir}
rm -rf %{buildroot}%{_datadir}/doc/weed-war

%files
%license LICENSE
%doc README.md
%{_bindir}/weed-war
%{_bindir}/weed-war-screensaver
%{_datadir}/applications/weed-war.desktop
%{_datadir}/icons/hicolor/256x256/apps/weed-war.png
%config(noreplace) %{_sysconfdir}/xdg/autostart/weed-war-screensaver.desktop
