Name:           mupager
Version:        0.2.0
Release:        1%{?dist}
Summary:        Terminal document viewer with pixel-perfect rendering

License:        MIT
URL:            https://github.com/joshheyse/mupager
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz

# CPM dependencies (header-only / build-time only)
Source1:        https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz#/doctest-2.4.12.tar.gz
Source2:        https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz#/tomlplusplus-3.4.0.tar.gz
Source3:        https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz#/CLI11-2.4.2.tar.gz
Source4:        https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz#/spdlog-1.15.0.tar.gz
Source5:        https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-7.0.0.tar.gz#/msgpack-cxx-7.0.0.tar.gz

BuildRequires:  cmake >= 3.26
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  git-core
BuildRequires:  pkg-config
BuildRequires:  mupdf-devel
BuildRequires:  ncurses-devel

# Bundled libraries (header-only, used at build time via CPM)
Provides:       bundled(doctest) = 2.4.12
Provides:       bundled(tomlplusplus) = 3.4.0
Provides:       bundled(CLI11) = 2.4.2
Provides:       bundled(spdlog) = 1.15.0
Provides:       bundled(msgpack-cxx) = 7.0.0

%description
mupager is a terminal document viewer that renders PDFs, EPUB, and other
documents with pixel-perfect fidelity using the Kitty graphics protocol.
It includes Neovim integration for viewing documents inside your editor.

%prep
%autosetup -n %{name}-%{version}

# Create a git repo with a version tag so CMake's git describe works
git init -q
git -c user.name=build -c user.email=build@build commit -q -m "v%{version}" --allow-empty
git tag v%{version}

# Unpack CPM dependency sources
mkdir -p _deps
tar xzf %{SOURCE1} -C _deps
tar xzf %{SOURCE2} -C _deps
tar xzf %{SOURCE3} -C _deps
tar xzf %{SOURCE4} -C _deps
tar xzf %{SOURCE5} -C _deps

%build
%cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCPM_doctest_SOURCE="%{_builddir}/%{name}-%{version}/_deps/doctest-2.4.12" \
  -DCPM_tomlplusplus_SOURCE="%{_builddir}/%{name}-%{version}/_deps/tomlplusplus-3.4.0" \
  -DCPM_CLI11_SOURCE="%{_builddir}/%{name}-%{version}/_deps/CLI11-2.4.2" \
  -DCPM_spdlog_SOURCE="%{_builddir}/%{name}-%{version}/_deps/spdlog-1.15.0" \
  -DCPM_msgpack-cxx_SOURCE="%{_builddir}/%{name}-%{version}/_deps/msgpack-c-cpp-7.0.0"
%cmake_build

%check
%ctest

%install
%cmake_install

# Remove CPM dependency artifacts that leak into the install
rm -rf %{buildroot}%{_includedir}/doctest
rm -rf %{buildroot}%{_libdir}/cmake/doctest

%files
%license LICENSE
%doc README.md
%{_bindir}/mupager
%{_mandir}/man1/mupager.1*
