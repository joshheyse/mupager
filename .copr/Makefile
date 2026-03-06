.PHONY: srpm

# COPR invokes: make -f .copr/Makefile srpm outdir=<path>
# from a git checkout of the repo (with network access).

SPEC = packaging/rpm/mupager.spec
VERSION = $(shell grep '^Version:' $(SPEC) | awk '{print $$2}')
NAME = mupager

srpm:
	mkdir -p $(outdir)
	# Source0: create tarball from git checkout
	git archive --format=tar.gz --prefix=$(NAME)-$(VERSION)/ HEAD \
		-o $(outdir)/$(NAME)-$(VERSION).tar.gz
	# Source1-5: download CPM dependency tarballs
	curl -Lo $(outdir)/doctest-2.4.12.tar.gz \
		https://github.com/doctest/doctest/archive/refs/tags/v2.4.12.tar.gz
	curl -Lo $(outdir)/tomlplusplus-3.4.0.tar.gz \
		https://github.com/marzer/tomlplusplus/archive/refs/tags/v3.4.0.tar.gz
	curl -Lo $(outdir)/CLI11-2.4.2.tar.gz \
		https://github.com/CLIUtils/CLI11/archive/refs/tags/v2.4.2.tar.gz
	curl -Lo $(outdir)/spdlog-1.15.0.tar.gz \
		https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.tar.gz
	curl -Lo $(outdir)/msgpack-cxx-7.0.0.tar.gz \
		https://github.com/msgpack/msgpack-c/archive/refs/tags/cpp-7.0.0.tar.gz
	# Build SRPM
	rpmbuild -bs $(SPEC) \
		--define "_sourcedir $(outdir)" \
		--define "_srcrpmdir $(outdir)" \
		--define "_topdir $(shell mktemp -d)"
