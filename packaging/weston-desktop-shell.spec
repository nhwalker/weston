Name:           weston-desktop-shell
Version:        14.0.2
Release:        1%{?dist}
Summary:        Weston's desktop shell, built out-of-tree against libweston

License:        MIT
URL:            https://github.com/nhwalker/weston
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  meson >= 0.63
BuildRequires:  pkgconfig(libweston-14)
BuildRequires:  pkgconfig(weston)
BuildRequires:  pkgconfig(wayland-client) >= 1.20.0
BuildRequires:  pkgconfig(wayland-server) >= 1.20.0
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  pkgconfig(wayland-protocols) >= 1.33
BuildRequires:  pkgconfig(pixman-1)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(cairo)
BuildRequires:  pkgconfig(libpng)
# Optional at build time; enables text rendering in toytoolkit titlebars.
BuildRequires:  pkgconfig(pango)
BuildRequires:  pkgconfig(pangocairo)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(glib-2.0)

# The shell plugin is dlopen()ed by the weston frontend and the helper
# clients are spawned from weston's libexecdir; the package is useless
# without a Weston 14 installation.
Requires:       weston

%description
Weston's desktop shell extracted from the Weston source tree and built
as a standalone project against an installed libweston, as an experiment
in out-of-tree shell development. It contains the compositor-side plugin
(desktop-shell.so, loaded with weston --shell=desktop-shell.so) and the
helper clients weston-desktop-shell (panel, background, launchers) and
weston-keyboard (on-screen keyboard).

%prep
%autosetup

%build
%meson
%meson_build

%install
%meson_install

%files
%license COPYING
%doc README.md
%{_libdir}/weston/desktop-shell.so
%{_libexecdir}/weston-desktop-shell
%{_libexecdir}/weston-keyboard
%dir %{_datadir}/weston
%{_datadir}/weston/icon_window.png
%{_datadir}/weston/pattern.png
%{_datadir}/weston/sign_close.png
%{_datadir}/weston/sign_maximize.png
%{_datadir}/weston/sign_minimize.png
%{_datadir}/weston/terminal.png

%changelog
* Tue Jul 07 2026 Nathan Walker <nathan.h.walker@gmail.com> - 14.0.2-1
- Initial package: Weston 14.0.2 desktop shell built out-of-tree
  against an installed libweston-14
