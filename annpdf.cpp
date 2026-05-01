#include <cstdint>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
// #include <unordered_map>
#include <vector>

// Boost
#include <boost/program_options.hpp>

// HarfBuzz & FreeType
#include <hb.h>
#include <hb-ft.h>
#include <ft2build.h>
#include FT_FREETYPE_H

// Cairo
#include <cairo.h>
#include <cairo-pdf.h>
#include <cairo-ft.h>

// Poppler (GLib interface)
#include <poppler.h>
#include <glib.h>

namespace fs = std::filesystem;
namespace po = boost::program_options;

class Annotation {
 public:
  virtual std::string str() const = 0;
};

class AnnotationText : public Annotation {
 public:
  AnnotationText(
    const std::string &lang,
    bool is_ltr,
    std::string text,
    int x,
    int y,
    const std::string &font_name,
    int font_size) :
    lang_{lang},
    is_ltr_{is_ltr},
    text_{text},
    xy_{x, y},
    font_name_{font_name},
    font_size_{font_size} {
  }
  std::string lang_{"en"}; // "en" or "he"
  bool is_ltr_{true};
  std::string text_;
  std::array<int, 2> xy_;
  std::string font_name_;
  int font_size_;
  std::string str() const override {
    return std::format("{}{}{} {} @ ({}, {}) F={}:{} {}",
      "{", lang_, is_ltr_ ? '>' : '<', text_, xy_[0], xy_[1],
      font_name_, font_size_,
      "}");
  }
 private:
  AnnotationText() = delete;
};

class AnnotationBlank : public Annotation {
 public:
  AnnotationBlank(int xl, int y, int xr, int h) :
    xy_{xl, y},
    width_{xr - xl},
    height_{h} {
  }
  std::string str() const override {
    return std::format("{}blank: xy=({}, {}) wh=({}, {}){}", "{", xy_[0],
                       xy_[1], width_, height_, "}");
  }
  std::array<int, 2> xy_;
  int width_{0};
  int height_{0};
};

class Font {
 public:
  ~Font() {
    if (hb_font_) {
      hb_font_destroy(hb_font_);
    }
    if (ft_face_newed_) {
      FT_Done_Face(ft_face_);
    }
  }
  FT_Face ft_face_;
  bool ft_face_newed_{false};
  hb_font_t *hb_font_{nullptr};
  int size{-1};
};

class AnnPdf {
 public:
  using vstrings_t = std::vector<std::string>;
  AnnPdf(
  const std::string &input_pdf_path,
  const std::string &output_pdf_path,
  const std::string &annotation_path,
  const vstrings_t &font_paths=vstrings_t(),
  uint32_t debug_flags=0) :
      input_pdf_path_{input_pdf_path},
      output_pdf_path_{output_pdf_path},
      annotation_path_{annotation_path},
      font_paths_{font_paths},
      debug_flags_{debug_flags} {
  }
  ~AnnPdf();
  int Run();
 private:
  void LoadPdf();
  void LoadFonts();
  int rc_{0};
  bool Ok() const { return rc_ == 0; }
  const std::string &input_pdf_path_;
  const std::string &output_pdf_path_;
  const std::string &annotation_path_;
  const vstrings_t &font_paths_;
  const uint32_t debug_flags_{0};
  PopplerDocument *doc_{nullptr};
  FT_Library ft_library_;
  bool ft_library_initded_{false};
  // std::unordered_map<std::string, Font;
};

AnnPdf::~AnnPdf() {
  if (ft_library_initded_) {
    FT_Done_FreeType(ft_library_);
  }
  if (doc_) {
    g_object_unref(doc_);
  }
}

int AnnPdf::Run() {
  if (debug_flags_ & 0x1) {
    std::cout << std::format(
      "input={}\n"
      "output={}\n"
      "annotation={}\n"
      "#(fonts)={}\n"
      "debug=0x{:x}\n",
      input_pdf_path_, output_pdf_path_, annotation_path_,
      font_paths_.size(), debug_flags_);
  }
  if (Ok()) { LoadPdf(); }
  if (Ok()) { LoadFonts(); }
  return rc_;
}

void AnnPdf::LoadPdf() {
  std::error_code ec;
  auto const sz = fs::file_size(input_pdf_path_, ec);
  if (ec) {
    std::cerr << std::format("file_size({}) failed reason={}\n",
      input_pdf_path_, ec.message());
    rc_ = ec.value();
  } else if (debug_flags_ & 0x2) {
    std::cout << std::format("size({})={}\n", input_pdf_path_, sz);
  }
  if (Ok()) {
    std::vector<char> bytes(sz, 0);
    std::ifstream file(input_pdf_path_, std::ios::binary);
    file.read(bytes.data(), sz);
    GBytes *g_bytes = g_bytes_new(bytes.data(), sz);
    GError* error = nullptr;
    doc_ = poppler_document_new_from_bytes(g_bytes, nullptr, &error);
  }
}

void AnnPdf::LoadFonts() {
  FT_Init_FreeType(&ft_library_);
  ft_library_initded_ = true;
  ////////////// hb_ft_font_changed(hb_font) !!!!!!!!!!!!!!!!!!!!!
}

namespace {

bool GetOptions(po::variables_map &vm, int argc, char **argv) {
  bool got = false;
  po::options_description desc("annpdf - Annotate PDF");
  desc.add_options()
   ("help,h", "produce help message")
   ("inpdf,i", po::value<std::string>()->required(),
        "Path to input .pdf file")
   ("outpdf,o", po::value<std::string>()->required(),
        "Path to output .pdf file")
   ("annotation,a", po::value<std::string>()->required(),
        "Path to annotation file")
   ("fonts,f", po::value<std::vector<std::string>>(),
        "Paths to .ttf or .otf fonts")
   ("debug,d", po::value<uint32_t>()->default_value(0), "debug flags");
  po::store(po::command_line_parser(argc, argv)
    .options(desc)
    .run(),
  vm);
  if (vm.count("help")) {
    std::cout << desc;
  } else {
    try {
      po::notify(vm);
      got = true;
    } catch (const po::error& e) {
      std::cerr << std::format("Error: {}\n", e.what()) << desc;
    }
  }
  return got;
}

} // namespace

int main(int argc, char **argv) {
  int rc = 0;
  po::variables_map vm;
  if (GetOptions(vm, argc, argv)) {
    AnnPdf ann_pdf(
      vm["inpdf"].as<std::string>(),
      vm["outpdf"].as<std::string>(),
      vm["annotation"].as<std::string>(),
      vm["fonts"].as<std::vector<std::string>>(),
      vm["debug"].as<uint32_t>());
    rc = ann_pdf.Run();
  }
  return rc;
}
