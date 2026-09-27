Name:           bucharest-lite
Version:        1.1.1
Release:        1%{?dist}
Summary:        Bucharest Lite - open-source non-linear video editor (NLE)

License:        GPL-3.0-or-later
URL:            https://github.com/wiczajac666/bucharestlite
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconf-pkg-config
BuildRequires:  qt6-qtbase-devel
BuildRequires:  ffmpeg-devel
BuildRequires:  json-devel
BuildRequires:  desktop-file-utils
BuildRequires:  appstream

%description
Bucharest Lite is an open-source (GPL-3.0-or-later) cross-platform non-linear
video editor (NLE) built on Qt6/C++17 with FFmpeg for media IO and GPU-accelerated
compositing.

It implements 20 essential NLE features including multi-track timeline, timeline
editing (trim/split/ripple), 3-point editing, frame-accurate playback, speed/time
remapping, keyframeable clip properties, transitions, video effects (color
correction, chroma key), layer-based compositing, titles/subtitles, multi-track
audio, scopes, modular codec support, proxy editing, project auto-save, unlimited
undo/redo, custom export, and cross-platform support.

%prep
%autosetup

%build
%cmake \
    -DBL_BUILD_TESTS=OFF \
    -DBL_BUILD_UI=ON \
    -DBL_BUILD_PLUGINS=ON \
    -DBL_BUILD_EXPORT=ON
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/bucharest-lite.desktop
appstreamcli validate --no-net %{buildroot}%{_datadir}/metainfo/com.github.harlemi.bucharestlite.metainfo.xml

%post
update-desktop-database %{_datadir}/applications &> /dev/null || :
gtk-update-icon-cache %{_datadir}/icons/hicolor &> /dev/null || :

%postun
gtk-update-icon-cache %{_datadir}/icons/hicolor &> /dev/null || :

%files
%{_bindir}/bucharest-lite
%{_datadir}/applications/bucharest-lite.desktop
%{_datadir}/metainfo/com.github.harlemi.bucharestlite.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/bucharest-lite.png
%{_datadir}/icons/hicolor/scalable/apps/bucharest-lite.svg
%{_datadir}/bucharest-lite/plugins/video/*.so
%{_datadir}/bucharest-lite/plugins/audio/*.so
%doc README.md FEATURES.md CHANGELOG.md PROJECT_STATUS.md

%changelog
* Sun Sep 27 2026 Harlemi <harlemi@www> - 1.1.1-1
- Release 1.1.1: alpha compositing, 5 new effects, speed/reverse editing, soft subtitles
- Rebuilt from v1.1.1 sources for x86_64

* Fri Sep 18 2026 Harlemi <harlemi@www> - 1.0.1-1
- Initial Fedora RPM packaging for Bucharest Lite 1.0.1
- Wire bl_export module into the cmake build (BL_BUILD_EXPORT)
- Install desktop entry, AppStream metainfo and hicolor icon set
- Bundle 9 FFmpeg-backed codec plugins (h264 vp9 av1 theora mpeg4 aac flac vorbis opus)