#include "graphics/kitty.hpp"

#include "document.hpp"
#include "graphics/pixmap.hpp"

#include <doctest/doctest.h>

#include <string>

static constexpr const char* FixturePdf = PROJECT_FIXTURE_DIR "/test.pdf";

TEST_CASE("kitty encode starts with ESC_G and ends with ST") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("kitty encode contains correct format and dimensions") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("f=100") != std::string::npos);
  CHECK(out.find("s=" + std::to_string(pix.width())) != std::string::npos);
  CHECK(out.find("v=" + std::to_string(pix.height())) != std::string::npos);
}

TEST_CASE("kitty encode includes image_id when non-zero") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 1.0f);

  std::string with_id = kitty::encode(pix, 42);
  CHECK(with_id.find("i=42") != std::string::npos);

  std::string without_id = kitty::encode(pix, 0);
  CHECK(without_id.find(",i=") == std::string::npos);
}

TEST_CASE("kitty encode last chunk has m=0") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("m=0") != std::string::npos);
}

TEST_CASE("kitty encode large image produces multiple chunks") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 2.0f);
  std::string out = kitty::encode(pix);

  CHECK(out.find("m=1") != std::string::npos);
  CHECK(out.find("m=0") != std::string::npos);
}

TEST_CASE("kitty delete_image") {
  CHECK(kitty::delete_image(7) == "\x1b_Ga=d,d=I,q=2,i=7\x1b\\");
}

TEST_CASE("kitty wrap_tmux doubles ESC bytes and adds DCS passthrough") {
  std::string apc = "\x1b_Ga=T,f=24;AAAA\x1b\\";
  std::string wrapped = kitty::wrap_tmux(apc);

  CHECK(wrapped.find("\x1bPtmux;") != std::string::npos);
  CHECK(wrapped.find("\x1b\x1b") != std::string::npos);
  // Should end with ST
  CHECK(wrapped.substr(wrapped.size() - 2) == "\x1b\\");
}

TEST_CASE("kitty wrap_tmux wraps multiple APC sequences individually") {
  std::string two_apcs = "\x1b_Gm=1;AAAA\x1b\\\x1b_Gm=0;BBBB\x1b\\";
  std::string wrapped = kitty::wrap_tmux(two_apcs);

  // Should contain two DCS passthrough blocks
  size_t first = wrapped.find("\x1bPtmux;");
  REQUIRE(first != std::string::npos);
  size_t second = wrapped.find("\x1bPtmux;", first + 1);
  CHECK(second != std::string::npos);
}

TEST_CASE("kitty transmit with cols/rows produces unicode placeholder mode") {
  Document doc(FixturePdf);
  Pixmap pix = doc.render_page(0, 1.0f);
  std::string out = kitty::transmit(pix, 1, 3, 5);

  CHECK(out.find("a=T") != std::string::npos);
  CHECK(out.find("U=1") != std::string::npos);
  CHECK(out.find("C=1") != std::string::npos);
  CHECK(out.find("c=3") != std::string::npos);
  CHECK(out.find("r=5") != std::string::npos);
}

// --- TransmitCommand tests ---

TEST_CASE("TransmitCommand serialize single chunk transmit-only") {
  kitty::TransmitCommand cmd;
  cmd.width = 2;
  cmd.height = 1;
  cmd.image_id = 7;
  std::string b64 = "AQID"; // short enough for one chunk
  std::string out = cmd.serialize(b64);

  CHECK(out.find("a=t") != std::string::npos);
  CHECK(out.find("a=T") == std::string::npos);
  CHECK(out.find("s=2") != std::string::npos);
  CHECK(out.find("v=1") != std::string::npos);
  CHECK(out.find("i=7") != std::string::npos);
  CHECK(out.find("m=0") != std::string::npos);
  CHECK(out.find("U=1") == std::string::npos);
  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("TransmitCommand serialize display mode") {
  kitty::TransmitCommand cmd;
  cmd.width = 4;
  cmd.height = 3;
  cmd.image_id = 1;
  cmd.placement_id = 2;
  cmd.action = kitty::TransmitAction::TransmitDisplay;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("a=T,") != std::string::npos);
  CHECK(out.find("p=2") != std::string::npos);
}

TEST_CASE("TransmitCommand serialize unicode mode with columns and rows") {
  kitty::TransmitCommand cmd;
  cmd.width = 16;
  cmd.height = 32;
  cmd.image_id = 3;
  cmd.action = kitty::TransmitAction::TransmitDisplay;
  cmd.unicode = true;
  cmd.columns = 2;
  cmd.rows = 4;
  std::string out = cmd.serialize("BBBB");

  CHECK(out.find("U=1") != std::string::npos);
  CHECK(out.find("c=2") != std::string::npos);
  CHECK(out.find("r=4") != std::string::npos);
}

TEST_CASE("TransmitCommand serialize multi-chunk") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.image_id = 1;
  // Create a payload larger than 4096 bytes to force multiple chunks
  std::string b64(5000, 'A');
  std::string out = cmd.serialize(b64);

  CHECK(out.find("m=1") != std::string::npos);
  CHECK(out.find("m=0") != std::string::npos);
  // First chunk has the header, continuation chunks only have m=
  size_t first_end = out.find("\x1b\\");
  REQUIRE(first_end != std::string::npos);
  std::string first_chunk = out.substr(0, first_end);
  CHECK(first_chunk.find("a=t") != std::string::npos);
  CHECK(first_chunk.find("f=24") != std::string::npos);
}

TEST_CASE("TransmitCommand omits image_id and placement_id when zero") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.action = kitty::TransmitAction::TransmitDisplay;
  std::string out = cmd.serialize("AA==");

  CHECK(out.find(",i=") == std::string::npos);
  CHECK(out.find(",p=") == std::string::npos);
}

TEST_CASE("TransmitCommand default produces backward-compatible output") {
  kitty::TransmitCommand cmd;
  cmd.width = 10;
  cmd.height = 20;
  cmd.image_id = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("a=t,") != std::string::npos);
  CHECK(out.find("t=d,") != std::string::npos);
  CHECK(out.find("f=24,") != std::string::npos);
  CHECK(out.find("q=2,") != std::string::npos);
}

TEST_CASE("TransmitCommand query action") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.action = kitty::TransmitAction::Query;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("a=q,") != std::string::npos);
}

TEST_CASE("TransmitCommand RGBA format") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.format = kitty::PixelFormat::Rgba;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("f=32") != std::string::npos);
}

TEST_CASE("TransmitCommand PNG format") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.format = kitty::PixelFormat::Png;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("f=100") != std::string::npos);
}

TEST_CASE("TransmitCommand file medium") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.medium = kitty::Medium::File;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("t=f,") != std::string::npos);
}

TEST_CASE("TransmitCommand temp file medium") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.medium = kitty::Medium::TempFile;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("t=t,") != std::string::npos);
}

TEST_CASE("TransmitCommand shared memory medium") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.medium = kitty::Medium::SharedMemory;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("t=s,") != std::string::npos);
}

TEST_CASE("TransmitCommand zlib compression") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.compression = kitty::Compression::Zlib;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("o=z") != std::string::npos);
}

TEST_CASE("TransmitCommand compression omitted when none") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find(",o=") == std::string::npos);
}

TEST_CASE("TransmitCommand data_size and data_offset") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.data_size = 1024;
  cmd.data_offset = 256;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("S=1024") != std::string::npos);
  CHECK(out.find("O=256") != std::string::npos);
}

TEST_CASE("TransmitCommand data_size and data_offset omitted when zero") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find(",S=") == std::string::npos);
  CHECK(out.find(",O=") == std::string::npos);
}

TEST_CASE("TransmitCommand image_number") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.image_number = 99;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("I=99") != std::string::npos);
}

TEST_CASE("TransmitCommand z_index") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.z_index = -1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("z=-1") != std::string::npos);
}

TEST_CASE("TransmitCommand cell offsets") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.cell_x_offset = 5;
  cmd.cell_y_offset = 3;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("X=5") != std::string::npos);
  CHECK(out.find("Y=3") != std::string::npos);
}

TEST_CASE("TransmitCommand do_not_move_cursor") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.do_not_move_cursor = true;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("C=1") != std::string::npos);
}

TEST_CASE("TransmitCommand do_not_move_cursor omitted when false") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find(",C=") == std::string::npos);
}

TEST_CASE("TransmitCommand relative placement fields") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.parent_image_id = 10;
  cmd.parent_placement_id = 20;
  cmd.horizontal_offset = -3;
  cmd.vertical_offset = 5;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("P=10") != std::string::npos);
  CHECK(out.find("Q=20") != std::string::npos);
  CHECK(out.find("H=-3") != std::string::npos);
  CHECK(out.find("V=5") != std::string::npos);
}

TEST_CASE("TransmitCommand relative placement omitted when zero") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find(",P=") == std::string::npos);
  CHECK(out.find(",Q=") == std::string::npos);
  CHECK(out.find(",H=") == std::string::npos);
  CHECK(out.find(",V=") == std::string::npos);
}

TEST_CASE("TransmitCommand custom quiet level") {
  kitty::TransmitCommand cmd;
  cmd.width = 1;
  cmd.height = 1;
  cmd.quiet = 0;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("q=0") != std::string::npos);
  CHECK(out.find("q=2") == std::string::npos);
}

// --- PlaceCommand tests ---

TEST_CASE("PlaceCommand serialize") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 5;
  cmd.placement_id = 1;
  cmd.src_x = 10;
  cmd.src_y = 20;
  cmd.src_w = 100;
  cmd.src_h = 200;
  CHECK(cmd.serialize() == "\x1b_Ga=p,q=2,i=5,p=1,x=10,y=20,w=100,h=200\x1b\\");
}

TEST_CASE("PlaceCommand columns and rows") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.columns = 5;
  cmd.rows = 10;
  std::string out = cmd.serialize();

  CHECK(out.find("c=5") != std::string::npos);
  CHECK(out.find("r=10") != std::string::npos);
}

TEST_CASE("PlaceCommand columns and rows omitted when zero") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize();

  CHECK(out.find(",c=") == std::string::npos);
  CHECK(out.find(",r=") == std::string::npos);
}

TEST_CASE("PlaceCommand z_index") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.z_index = -2;
  std::string out = cmd.serialize();

  CHECK(out.find("z=-2") != std::string::npos);
}

TEST_CASE("PlaceCommand cell offsets") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.cell_x_offset = 4;
  cmd.cell_y_offset = 8;
  std::string out = cmd.serialize();

  CHECK(out.find("X=4") != std::string::npos);
  CHECK(out.find("Y=8") != std::string::npos);
}

TEST_CASE("PlaceCommand do_not_move_cursor") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.do_not_move_cursor = true;
  std::string out = cmd.serialize();

  CHECK(out.find("C=1") != std::string::npos);
}

TEST_CASE("PlaceCommand unicode") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.unicode = true;
  std::string out = cmd.serialize();

  CHECK(out.find("U=1") != std::string::npos);
}

TEST_CASE("PlaceCommand relative placement fields") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.parent_image_id = 5;
  cmd.parent_placement_id = 7;
  cmd.horizontal_offset = 2;
  cmd.vertical_offset = -1;
  std::string out = cmd.serialize();

  CHECK(out.find("P=5") != std::string::npos);
  CHECK(out.find("Q=7") != std::string::npos);
  CHECK(out.find("H=2") != std::string::npos);
  CHECK(out.find("V=-1") != std::string::npos);
}

TEST_CASE("PlaceCommand custom quiet level") {
  kitty::PlaceCommand cmd;
  cmd.image_id = 1;
  cmd.quiet = 1;
  std::string out = cmd.serialize();

  CHECK(out.find("q=1") != std::string::npos);
  CHECK(out.find("q=2") == std::string::npos);
}

// --- DeleteCommand tests ---

TEST_CASE("DeleteCommand serialize by image ID") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ById;
  cmd.image_id = 42;
  CHECK(cmd.serialize() == "\x1b_Ga=d,d=i,q=2,i=42\x1b\\");
}

TEST_CASE("DeleteCommand serialize all placements") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::All;
  CHECK(cmd.serialize() == "\x1b_Ga=d,d=a,q=2\x1b\\");
}

TEST_CASE("DeleteCommand free flag uses uppercase target") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::All;
  cmd.free = true;
  CHECK(cmd.serialize() == "\x1b_Ga=d,d=A,q=2\x1b\\");
}

TEST_CASE("DeleteCommand by ID with placement ID") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ById;
  cmd.image_id = 10;
  cmd.placement_id = 3;
  std::string out = cmd.serialize();

  CHECK(out.find("d=i") != std::string::npos);
  CHECK(out.find("i=10") != std::string::npos);
  CHECK(out.find("p=3") != std::string::npos);
}

TEST_CASE("DeleteCommand by ID with free flag") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ById;
  cmd.free = true;
  cmd.image_id = 5;
  std::string out = cmd.serialize();

  CHECK(out.find("d=I") != std::string::npos);
  CHECK(out.find("i=5") != std::string::npos);
}

TEST_CASE("DeleteCommand by image number") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ByNumber;
  cmd.image_number = 77;
  std::string out = cmd.serialize();

  CHECK(out.find("d=n") != std::string::npos);
  CHECK(out.find("I=77") != std::string::npos);
}

TEST_CASE("DeleteCommand at cursor") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::AtCursor;
  std::string out = cmd.serialize();

  CHECK(out.find("d=c") != std::string::npos);
}

TEST_CASE("DeleteCommand at position") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::AtPosition;
  cmd.x = 5;
  cmd.y = 10;
  std::string out = cmd.serialize();

  CHECK(out.find("d=p") != std::string::npos);
  CHECK(out.find("x=5") != std::string::npos);
  CHECK(out.find("y=10") != std::string::npos);
}

TEST_CASE("DeleteCommand at position with z-index") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::AtPositionZ;
  cmd.x = 2;
  cmd.y = 3;
  cmd.z = -1;
  std::string out = cmd.serialize();

  CHECK(out.find("d=q") != std::string::npos);
  CHECK(out.find("x=2") != std::string::npos);
  CHECK(out.find("y=3") != std::string::npos);
  CHECK(out.find("z=-1") != std::string::npos);
}

TEST_CASE("DeleteCommand by column") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ByColumn;
  cmd.x = 15;
  std::string out = cmd.serialize();

  CHECK(out.find("d=x") != std::string::npos);
  CHECK(out.find("x=15") != std::string::npos);
}

TEST_CASE("DeleteCommand by row") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ByRow;
  cmd.y = 20;
  std::string out = cmd.serialize();

  CHECK(out.find("d=y") != std::string::npos);
  CHECK(out.find("y=20") != std::string::npos);
}

TEST_CASE("DeleteCommand by z-index") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ByZIndex;
  cmd.z = 3;
  std::string out = cmd.serialize();

  CHECK(out.find("d=z") != std::string::npos);
  CHECK(out.find("z=3") != std::string::npos);
}

TEST_CASE("DeleteCommand by ID range") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::ByIdRange;
  cmd.x = 10;
  cmd.y = 50;
  std::string out = cmd.serialize();

  CHECK(out.find("d=r") != std::string::npos);
  CHECK(out.find("x=10") != std::string::npos);
  CHECK(out.find("y=50") != std::string::npos);
}

TEST_CASE("DeleteCommand animation frames") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::Frames;
  cmd.image_id = 8;
  std::string out = cmd.serialize();

  CHECK(out.find("d=f") != std::string::npos);
  CHECK(out.find("i=8") != std::string::npos);
}

TEST_CASE("DeleteCommand custom quiet level") {
  kitty::DeleteCommand cmd;
  cmd.target = kitty::DeleteTarget::All;
  cmd.quiet = 0;
  std::string out = cmd.serialize();

  CHECK(out.find("q=0") != std::string::npos);
  CHECK(out.find("q=2") == std::string::npos);
}

// --- AnimationFrameCommand tests ---

TEST_CASE("AnimationFrameCommand serialize defaults") {
  kitty::AnimationFrameCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("a=f") != std::string::npos);
  CHECK(out.find("t=d") != std::string::npos);
  CHECK(out.find("f=32") != std::string::npos);
  CHECK(out.find("q=2") != std::string::npos);
  CHECK(out.find("i=1") != std::string::npos);
  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("AnimationFrameCommand serialize with all fields") {
  kitty::AnimationFrameCommand cmd;
  cmd.image_id = 5;
  cmd.format = kitty::PixelFormat::Rgb;
  cmd.medium = kitty::Medium::File;
  cmd.compression = kitty::Compression::Zlib;
  cmd.data_size = 2048;
  cmd.data_offset = 100;
  cmd.x = 10;
  cmd.y = 20;
  cmd.width = 64;
  cmd.height = 32;
  cmd.base_frame = 1;
  cmd.edit_frame = 2;
  cmd.frame_gap = 40;
  cmd.composition_mode = 1;
  cmd.background_color = 0xFF0000FF;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find("a=f") != std::string::npos);
  CHECK(out.find("t=f") != std::string::npos);
  CHECK(out.find("f=24") != std::string::npos);
  CHECK(out.find("o=z") != std::string::npos);
  CHECK(out.find("i=5") != std::string::npos);
  CHECK(out.find("S=2048") != std::string::npos);
  CHECK(out.find("O=100") != std::string::npos);
  CHECK(out.find("x=10") != std::string::npos);
  CHECK(out.find("y=20") != std::string::npos);
  CHECK(out.find("s=64") != std::string::npos);
  CHECK(out.find("v=32") != std::string::npos);
  CHECK(out.find("c=1") != std::string::npos);
  CHECK(out.find("r=2") != std::string::npos);
  CHECK(out.find("z=40") != std::string::npos);
  CHECK(out.find("X=1") != std::string::npos);
  CHECK(out.find("Y=4278190335") != std::string::npos);
}

TEST_CASE("AnimationFrameCommand omits optional fields when default") {
  kitty::AnimationFrameCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize("AAAA");

  CHECK(out.find(",S=") == std::string::npos);
  CHECK(out.find(",O=") == std::string::npos);
  CHECK(out.find(",x=") == std::string::npos);
  CHECK(out.find(",y=") == std::string::npos);
  CHECK(out.find(",s=") == std::string::npos);
  CHECK(out.find(",v=") == std::string::npos);
  CHECK(out.find(",c=") == std::string::npos);
  CHECK(out.find(",r=") == std::string::npos);
  CHECK(out.find(",z=") == std::string::npos);
  CHECK(out.find(",X=") == std::string::npos);
  CHECK(out.find(",Y=") == std::string::npos);
}

// --- AnimationControlCommand tests ---

TEST_CASE("AnimationControlCommand serialize defaults") {
  kitty::AnimationControlCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize();

  CHECK(out.find("a=a") != std::string::npos);
  CHECK(out.find("q=2") != std::string::npos);
  CHECK(out.find("i=1") != std::string::npos);
  CHECK(out.find("s=1") != std::string::npos);
  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("AnimationControlCommand serialize with all fields") {
  kitty::AnimationControlCommand cmd;
  cmd.image_id = 3;
  cmd.image_number = 7;
  cmd.state = kitty::AnimationState::Run;
  cmd.loop_count = 5;
  cmd.current_frame = 2;
  cmd.target_frame = 4;
  cmd.frame_gap = 100;
  std::string out = cmd.serialize();

  CHECK(out.find("i=3") != std::string::npos);
  CHECK(out.find("I=7") != std::string::npos);
  CHECK(out.find("s=3") != std::string::npos);
  CHECK(out.find("v=5") != std::string::npos);
  CHECK(out.find("c=2") != std::string::npos);
  CHECK(out.find("r=4") != std::string::npos);
  CHECK(out.find("z=100") != std::string::npos);
}

TEST_CASE("AnimationControlCommand LOADING state") {
  kitty::AnimationControlCommand cmd;
  cmd.image_id = 1;
  cmd.state = kitty::AnimationState::Loading;
  std::string out = cmd.serialize();

  CHECK(out.find("s=2") != std::string::npos);
}

TEST_CASE("AnimationControlCommand omits optional fields when default") {
  kitty::AnimationControlCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize();

  CHECK(out.find(",I=") == std::string::npos);
  CHECK(out.find(",v=") == std::string::npos);
  CHECK(out.find(",c=") == std::string::npos);
  CHECK(out.find(",r=") == std::string::npos);
  CHECK(out.find(",z=") == std::string::npos);
}

// --- ComposeCommand tests ---

TEST_CASE("ComposeCommand serialize defaults") {
  kitty::ComposeCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize();

  CHECK(out.find("a=c") != std::string::npos);
  CHECK(out.find("q=2") != std::string::npos);
  CHECK(out.find("i=1") != std::string::npos);
  CHECK(out.substr(0, 2) == "\x1b_");
  CHECK(out.substr(out.size() - 2) == "\x1b\\");
}

TEST_CASE("ComposeCommand serialize with all fields") {
  kitty::ComposeCommand cmd;
  cmd.image_id = 2;
  cmd.src_frame = 1;
  cmd.dst_frame = 3;
  cmd.src_x = 10;
  cmd.src_y = 20;
  cmd.width = 100;
  cmd.height = 50;
  cmd.dst_x = 5;
  cmd.dst_y = 15;
  cmd.blend_mode = 1;
  std::string out = cmd.serialize();

  CHECK(out.find("i=2") != std::string::npos);
  CHECK(out.find("r=1") != std::string::npos);
  CHECK(out.find("c=3") != std::string::npos);
  CHECK(out.find("x=10") != std::string::npos);
  CHECK(out.find("y=20") != std::string::npos);
  CHECK(out.find("w=100") != std::string::npos);
  CHECK(out.find("h=50") != std::string::npos);
  CHECK(out.find("X=5") != std::string::npos);
  CHECK(out.find("Y=15") != std::string::npos);
  CHECK(out.find("C=1") != std::string::npos);
}

TEST_CASE("ComposeCommand omits optional fields when default") {
  kitty::ComposeCommand cmd;
  cmd.image_id = 1;
  std::string out = cmd.serialize();

  CHECK(out.find(",r=") == std::string::npos);
  CHECK(out.find(",c=") == std::string::npos);
  CHECK(out.find(",x=") == std::string::npos);
  CHECK(out.find(",y=") == std::string::npos);
  CHECK(out.find(",w=") == std::string::npos);
  CHECK(out.find(",h=") == std::string::npos);
  CHECK(out.find(",X=") == std::string::npos);
  CHECK(out.find(",Y=") == std::string::npos);
  CHECK(out.find(",C=") == std::string::npos);
}
