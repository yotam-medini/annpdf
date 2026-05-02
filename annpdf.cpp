#include <cstdint>

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <sysexits.h>


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

#include "annparse.hpp"

namespace fs = std::filesystem;
namespace po = boost::program_options;

class Font {
 public:
  ~Font() {
    if (c_face_) {
      cairo_font_face_destroy(c_face_);
    }
    if (hb_font_) {
      hb_font_destroy(hb_font_);
    }
    if (ft_face_) {
      FT_Done_Face(ft_face_);
    }
  }
  void Resize(int size) {
    if (size_ != size) {
std::cout << std::format("Resize: {} -> {}\n", size_, size);
      FT_Set_Char_Size(ft_face_, 0, size * 64, 72, 72);
      // hb_ft_font_changed(hb_font_);
      if (hb_font_) {
        hb_font_destroy(hb_font_);
      }
      hb_font_ = hb_ft_font_create(ft_face_, nullptr);
      hb_ft_font_set_funcs(hb_font_);

      if (c_face_) {
        cairo_font_face_destroy(c_face_);
      }
      c_face_ = cairo_ft_font_face_create_for_ft_face(ft_face_, 0);
      size_ = size;
    }
  }
  FT_Face ft_face_{nullptr};
  hb_font_t *hb_font_{nullptr};
  cairo_font_face_t *c_face_{nullptr};
  int size_{12}; // default
};

class Annotation {
 public:
  Annotation(int page=0) : page_{page} {}
  virtual ~Annotation() {}
  virtual std::string str() const = 0;
  int page_;
};

class AnnotationText : public Annotation {
 public:
  AnnotationText(
    int page,
    const std::string &lang,
    bool is_ltr,
    std::string text,
    std::array<int, 2> xy,
    const std::string &font_name,
    int font_size) :
    Annotation(page),
    lang_{lang},
    is_ltr_{is_ltr},
    text_{text},
    xy_{xy},
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
    return std::format("{}p={} {}{} @ ({}, {}) F={}:{} {} T={}",
      "{", page_, lang_, is_ltr_ ? '>' : '<', xy_[0], xy_[1],
      font_name_, font_size_, text_,
      "}");
  }
 private:
  AnnotationText() = delete;
};

class AnnotationBlank : public Annotation {
 public:
  AnnotationBlank(int page, int xl, int y, int xr, int h) :
    Annotation(page),
    xy_{xl, y},
    width_{xr - xl},
    height_{h} {
  }
  std::string str() const override {
    return std::format("{}blank: p={}, xy=({}, {}) wh=({}, {}){}",
      "{", page_, xy_[0], xy_[1], width_, height_, "}");
  }
  std::array<int, 2> xy_;
  int width_{0};
  int height_{0};
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
  void LoadAnnotations();
  void LoadAnnotationText(
    AnnParseState &ps,
    const std::string &line,
    int line_number);
  void Annotate();
  void AnnotatePage(
    PopplerPage* page, cairo_t* cr, const size_t ai_begin, const size_t ai_end);
  void HarfBuzzShaping(
    hb_buffer_t* hb_buffer,
    const AnnotationText &at,
    const Font &font);
  void ApplyFont(cairo_t *cr, const Font &font);
  void ShowGlyphs(
    cairo_t *cr,
    hb_buffer_t* hb_buffer,
    const AnnotationText &at);
  void SetRc(int code) { if (rc_ == 0) { rc_ = code; } }
  bool Ok() const { return rc_ == 0; }

  int rc_{0};
  const std::string &input_pdf_path_;
  const std::string &output_pdf_path_;
  const std::string &annotation_path_;
  const vstrings_t &font_paths_;
  const uint32_t debug_flags_{0};
  std::vector<std::unique_ptr<Annotation>> annotations_;
  PopplerDocument *doc_{nullptr};
  FT_Library ft_library_;
  bool ft_library_initded_{false};
  std::unordered_map<std::string, Font> name2font_;
};

AnnPdf::~AnnPdf() {
  name2font_.clear(); // before freeing ft_library_
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
  if (Ok()) { LoadAnnotations(); }
  if (Ok()) { Annotate(); }
  return rc_;
}

void AnnPdf::LoadPdf() {
  std::error_code ec;
  auto const sz = fs::file_size(input_pdf_path_, ec);
  if (ec) {
    std::cerr << std::format("file_size({}) failed reason={}\n",
      input_pdf_path_, ec.message());
    SetRc(ec.value());
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
  for (auto const &s_font_path : font_paths_) {
    fs::path font_path{s_font_path};
    std::string font_name = font_path.filename();
    if (font_name.ends_with(".ttf") || font_name.ends_with(".otf")) {
      font_name.erase(font_name.end() - 4, font_name.end());
    }
    if (debug_flags_ & 0x2) {
      std::cout << std::format("s_font_path={}, font_name={}\n",
        s_font_path, font_name);
      if (name2font_.find(font_name) != name2font_.end()) {
        std::cerr << std::format("Duplicate font name: {}\n", font_name);
        SetRc(1);
      } else {
        auto iter = name2font_.insert({font_name, Font()}).first;
        Font &font = iter->second;
        FT_Face &ft_face = font.ft_face_;
        if (FT_New_Face(ft_library_, s_font_path.c_str(), 0, &ft_face)) {
          std::cerr << std::format("Failed to load font {}\n", s_font_path);
          SetRc(EX_NOINPUT);
        }
        if (Ok()) {
          FT_Set_Char_Size(ft_face, 0, font.size_ * 64, 72, 72);
          font.hb_font_ = hb_ft_font_create(ft_face, nullptr);
          font.c_face_ = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
        }
      }
    }
  }
}

void AnnPdf::LoadAnnotations() {
  std::ifstream f{annotation_path_};
  if (f.fail()) {
    std::cerr << std::format("Failed to open annotation file {}\n",
      annotation_path_);
    SetRc(EX_NOINPUT);
  } else {
    int line_number = 0;
    AnnParseState ps;
    for (std::string line; Ok() && std::getline(f, line); ++line_number) {
      if (debug_flags_ & 0x10) {
        std::cout << std::format("[{:3d}] {}\n", line_number, line);
      }
      char c0 = line.empty() ? ' ' : line[0];
      if ((c0 != ' ') && (c0 != '#')) {
        if (line.starts_with(blank)) {
          rc_ = ps.ParseBlank(line, line_number);
          if (Ok()) {
            annotations_.push_back(
              std::make_unique<AnnotationBlank>(
                ps.page_, ps.x_, ps.y_, ps.width_, ps.height_));
          }
        } else {
          LoadAnnotationText(ps, line, line_number);
        }
      }
    }
  }
  if (Ok() && (debug_flags_ & 0x4)) {
    std::cerr << std::format("annotations[{}]", annotations_.size()) << "{\n";
    for (size_t i = 0; i < annotations_.size(); ++i) {
      std::cerr << std::format("  [{:4d}] {}\n", i, annotations_[i]->str());
    }
    std::cerr << "}\n";
  }
}

void AnnPdf::LoadAnnotationText(
    AnnParseState &ps,
    const std::string &line,
    int line_number) {
  // page:x:y:font:size:lang:dir:text  # 7 colons max before text
  const auto ld = line.data();
  const auto ld_end = ld + line.size();
  const size_t col_count = std::count(ld, ld_end, ':');
  if (col_count == 0) {
    std::cerr << std::format("No colons in [{}] {}\n",
      line_number, line);
    SetRc(EX_CONFIG);
  } else {
    size_t text_pos = 0;
    if (col_count <= 7) {
      text_pos = line.rfind(':') + 1;
    } else {
      for (size_t cc = 0; cc < 7; ++cc) {
        auto col_pos = std::find(ld + text_pos, ld_end, ':');
        text_pos += (col_pos - ld) + 1;
      }
    }
    auto const pre_text = line.substr(0, text_pos);
    auto const text = line.substr(text_pos);
    auto lang_old = ps.lang_;
    auto font_old = ps.font_name_;
    rc_ = ps.ParseText(pre_text, line_number);
    if (Ok() && (lang_old != ps.lang_)) {
      auto const hb_lang = hb_language_from_string(ps.lang_.c_str(), -1);
      if (hb_lang == HB_LANGUAGE_INVALID) {
        std::cerr << std::format("Unsupported lang: {}\n", ps.lang_);
        SetRc(EX_CONFIG);
      }
    }
    if (Ok() && (font_old != ps.font_name_)) {
      if (name2font_.find(ps.font_name_) == name2font_.end()) {
        std::cerr << std::format("Not found font: {}\n", ps.font_name_);
        SetRc(EX_CONFIG);
      }
    }
    if (Ok()) {
      annotations_.push_back(
        std::make_unique<AnnotationText>(
          ps.page_, ps.lang_, ps.dir_ == "ltr", text, ps.xy(),
          ps.font_name_, ps.font_size_));
    }
  }
}

void AnnPdf::Annotate() {
  const int num_pages = poppler_document_get_n_pages(doc_);
  const size_t num_anns = annotations_.size();
  if (debug_flags_ & 0x1) {
    std::cout << std::format("Annotate: num_pages={}\n", num_pages);
  }
  cairo_surface_t* surface =
    cairo_pdf_surface_create(output_pdf_path_.c_str(), 1, 1);
  cairo_t* cr = nullptr;
  if (!surface) {
    std::cerr << std::format("Failed cairo_pdf_surface_create({})\n",
      output_pdf_path_);
    SetRc(EX_UNAVAILABLE);
  } else {
    auto c_status = cairo_surface_status(surface);
    if (debug_flags_ & 0x1) {
      std::cout << std::format("cairo_surface_status: {}\n",
        static_cast<int>(c_status));
    }
    cr = cairo_create(surface);
  }
  size_t ann_idx = 0;
  for (int page_num = 0, p1 = 1; Ok() && (page_num < num_pages);
      page_num = p1++) {

    PopplerPage* page = poppler_document_get_page(doc_, page_num);
    double w, h;
    poppler_page_get_size(page, &w, &h);

    cairo_pdf_surface_set_size(surface, w, h);
    poppler_page_render(page, cr);

    if (debug_flags_ & 0x1) {
      std::cout << std::format("Annotating/Copying page: {} (+1={})\n",
        page_num, p1);
    }
    while ((ann_idx < num_anns) && (annotations_[ann_idx]->page_ < p1)) {
       ++ann_idx;
    }
    auto const ann_idx_begin = ann_idx;
    while ((ann_idx < num_anns) && (annotations_[ann_idx]->page_ == p1)) {
      ++ann_idx;
    }
    if (ann_idx_begin < ann_idx) {
      AnnotatePage(page, cr, ann_idx_begin, ann_idx);
    }

    cairo_show_page(cr); // saving to file
    g_object_unref(page);
  }
  cairo_destroy(cr);
  if (surface) {
    cairo_surface_destroy(surface);
  }
}

void AnnPdf::AnnotatePage(
    PopplerPage* page,
    cairo_t* cr,
    const size_t ai_begin,
    const size_t ai_end) {
  double w, h;
  poppler_page_get_size(page, &w, &h);
  cairo_save(cr);

  // Cartesian Flip
  cairo_translate(cr, 0, h);
  cairo_scale(cr, 1.0, -1.0);

  for (size_t ai = ai_begin; Ok() && (ai < ai_end); ++ai) {
    const Annotation *a = annotations_[ai].get();
    const AnnotationBlank *ab = dynamic_cast<const AnnotationBlank*>(a);
    const AnnotationText *at = dynamic_cast<const AnnotationText*>(a);
    if (ab) {
      cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // white
      cairo_rectangle(cr, ab->xy_[0], ab->xy_[1], ab->width_, ab->height_);
      cairo_fill(cr);
    }
    if (at) {
      std::unordered_map<std::string, Font>::iterator iter =
        name2font_.find(at->font_name_);
      if (iter == name2font_.end()) {
        std::cerr << std::format("Fatal not found font={}\n", at->font_name_);
        exit(13);
      }
      Font &font = iter->second;
      font.Resize(at->font_size_);

      hb_buffer_t* hb_buffer = hb_buffer_create();
      HarfBuzzShaping(hb_buffer, *at, font);
      ApplyFont(cr, font);
      ShowGlyphs(cr, hb_buffer, *at);
      hb_buffer_destroy(hb_buffer);
    }
  }
  cairo_restore(cr);
}

void AnnPdf::HarfBuzzShaping(
    hb_buffer_t* hb_buffer,
    const AnnotationText &at,
    const Font &font) {
std::cerr << std::format("{} text={}\n", __func__, at.text_);
  hb_buffer_add_utf8(hb_buffer, at.text_.c_str(), -1, 0, -1);
  hb_direction_t hb_dir = at.is_ltr_ ? HB_DIRECTION_LTR : HB_DIRECTION_RTL;
  hb_buffer_set_direction(hb_buffer, hb_dir);
  hb_buffer_set_script(hb_buffer,
    (hb_dir == HB_DIRECTION_RTL) ? HB_SCRIPT_HEBREW : HB_SCRIPT_LATIN);
  auto hb_lang = hb_language_from_string(at.lang_.c_str(), -1);
  hb_buffer_set_language(hb_buffer, hb_lang);
  hb_shape(font.hb_font_, hb_buffer, nullptr, 0);
}

void AnnPdf::ApplyFont(cairo_t *cr, const Font &font) {
  cairo_set_font_face(cr, nullptr); // drop cache
  cairo_set_font_face(cr, font.c_face_);
  cairo_matrix_t font_matrix;
  cairo_matrix_init(&font_matrix, font.size_, 0, 0, -font.size_, 0, 0);
  cairo_set_font_matrix(cr, &font_matrix);
  cairo_set_source_rgb(cr, 0, 0, 0);
}

void AnnPdf::ShowGlyphs(
    cairo_t *cr,
    hb_buffer_t* hb_buffer,
    const AnnotationText &at) {
  unsigned int glyph_count;
  hb_glyph_info_t* info = hb_buffer_get_glyph_infos(hb_buffer, &glyph_count);
  hb_glyph_position_t* pos =
    hb_buffer_get_glyph_positions(hb_buffer, &glyph_count);

  double cx = at.xy_[0];
  double cy = at.xy_[1];
  std::vector<cairo_glyph_t> cairo_glyphs(glyph_count);

std::cerr << std::format("glyph_count={}\n", glyph_count);
  for (unsigned int g = 0; g < glyph_count; ++g) {
      cairo_glyphs[g].index = info[g].codepoint;
      cairo_glyphs[g].x = cx + (pos[g].x_offset / 64.0);
std::cerr << std::format("g={} x_offset={} x_advance={} x={}\n",
 g, pos[g].x_offset, pos[g].x_advance, cairo_glyphs[g].x);
      cairo_glyphs[g].y = cy - (pos[g].y_offset / 64.0);
      cx += pos[g].x_advance / 64.0;
      cy += pos[g].y_advance / 64.0;
  }

  cairo_show_glyphs(cr, cairo_glyphs.data(), glyph_count);
}

namespace {

bool GetOptions(po::variables_map &vm, int &rc, int argc, char **argv) {
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
      rc = 1;
    }
  }
  return got;
}

} // namespace

int main(int argc, char **argv) {
  int rc = 0;
  po::variables_map vm;
  if (GetOptions(vm, rc, argc, argv)) {
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
