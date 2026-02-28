// Smoke test: verify all dependencies compile and link.

// MuPDF (C API)
#include <mupdf/fitz.h>

// notcurses (C++ API)
#include <ncpp/NotCurses.hh>
#include <ncpp/Visual.hh>

// msgpack-cxx
#include <msgpack.hpp>

// toml++ (header-only)
#include <toml++/toml.hpp>

// CLI11 (header-only)
#include <CLI/CLI.hpp>

// spdlog
#include <spdlog/spdlog.h>

int main(int argc, char* argv[]) {
  // -- CLI11 --
  CLI::App app{"mupdf-server - terminal document viewer"};
  std::string file;
  app.add_option("file", file, "Document to open")->required();
  CLI11_PARSE(app, argc, argv);

  // -- spdlog --
  spdlog::info("mupdf-server starting: {}", file);

  // -- MuPDF --
  fz_context* ctx = fz_new_context(nullptr, nullptr, FZ_STORE_DEFAULT);
  if (!ctx) {
    spdlog::error("failed to create mupdf context");
    return 1;
  }
  fz_register_document_handlers(ctx);
  spdlog::info("mupdf context created");

  fz_document* doc = nullptr;
  fz_try(ctx) {
    doc = fz_open_document(ctx, file.c_str());
  }
  fz_catch(ctx) {
    spdlog::error("failed to open document: {}", fz_caught_message(ctx));
    fz_drop_context(ctx);
    return 1;
  }

  int pages = fz_count_pages(ctx, doc);
  spdlog::info("opened document: {} pages", pages);

  fz_drop_document(ctx, doc);
  fz_drop_context(ctx);

  // -- toml++ --
  auto tbl = toml::parse("title = 'smoke test'");
  spdlog::info("toml++ parsed: title = {}", tbl["title"].value_or("???"));

  // -- msgpack --
  msgpack::sbuffer sbuf;
  msgpack::pack(sbuf, std::string("hello msgpack"));
  spdlog::info("msgpack packed {} bytes", sbuf.size());

  // -- notcurses (just check the version, don't init a terminal) --
  spdlog::info("notcurses version: {}", notcurses_version());

  spdlog::info("all dependencies OK");
  return 0;
}
