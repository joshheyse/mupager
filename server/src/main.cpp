#include <cstdio>

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: mupdf-server <file>\n");
    return 1;
  }

  std::printf("mupdf-server: %s\n", argv[1]);
  return 0;
}
