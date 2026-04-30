#include <cstdint>

#include <array>
#include <format>
#include <iostream>
#include <string>
#include <vector>
#include <fstream>

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

namespace po = boost::program_options;

class Annotation {
 public:
  virtual std::string str() const = 0;
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
  int Run();
 private:
  int rc_{0};
  bool Ok() const { return rc_ == 0; }
  const std::string &input_pdf_path_;
  const std::string &output_pdf_path_;
  const std::string &annotation_path_;
  const vstrings_t &font_paths_;
  const uint32_t debug_flags_{0};
};

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
  return rc_;
}

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
