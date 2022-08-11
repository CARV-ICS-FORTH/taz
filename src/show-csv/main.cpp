// Copyright 2022 Fabien Chaix
// SPDX-License-Identifier: LGPL-2.1-only
#include "../common/common.hpp"

constexpr char Separators[] = ";,";

struct FileContent {
  std::vector<std::vector<std::string>> content;
  std::vector<size_t> col_width;
  size_t ncols;
  size_t nrows;
};

FileContent parse_csv_file(const char *filename) {
  FileContent res;
  std::ifstream stream(filename);
  assert_always(stream,
                "Cannot open file " << filename << " to parse CSV content");
  std::string buff;

  // Parse the header
  std::getline(stream, buff);
  GenericParser parser;
  const char *ptr = parser.init_parsing(buff.data(), Separators);
  while (ptr) {
    res.content.push_back({ptr});
    res.col_width.push_back(strlen(ptr));
    ptr = parser.try_next_token(Separators);
  }

  res.ncols = res.col_width.size();
  assert_always(res.ncols > 0,
                "Must have strictly positive number of column in file "
                    << filename);

  // Parse column
  for (;;) {
    std::getline(stream, buff);
    if (not stream.good())
      break;
    const char *ptr = parser.init_parsing(buff.data(), Separators);
    for (size_t i = 0; i < res.ncols; i++) {
      res.content[i].push_back(ptr);
      res.col_width[i] = std::max(res.col_width[i], strlen(ptr));
      if (i + 1 < res.ncols)
        ptr = parser.get_next_token(Separators);
    }
    parser.check_last_token();
  }
  res.nrows = res.content[0].size();
  return res;
}

std::string pad_string(const std::string &s, size_t size) {
  std::string res;
  assert(size >= s.size());
  size_t padding = size - s.size();
  size_t i = 0;
  for (; i < padding / 2; i++)
    res += ' ';
  res += s;
  for (; i < padding; i++)
    res += ' ';
  return res;
}

void show_file_content(const FileContent &content) {

  std::cout << "Table of dimension " << content.nrows << "x" << content.ncols
            << std::endl;
  std::cout.fill(' ');
  size_t total_width = content.ncols + 1;
  for (auto &s : content.col_width)
    total_width += s;
  for (size_t i = 0; i < content.nrows; i++) {
    std::cout << '|';
    for (size_t j = 0; j < content.ncols; j++) {
      std::cout << pad_string(content.content[j][i], content.col_width[j])
                << '|';
    }
    if (i == 0) {
      std::cout << std::endl;
      for (size_t k = 0; k < total_width; k++)
        std::cout << "-";
    }
    std::cout << std::endl;
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cout << "Syntax: <CSV file1> ... [CSV file N]" << std::endl;
    return -1;
  }

  for (int i = 1; i < argc; i++) {
    std::cout << "Opening CSV table from <" << argv[i] << ">..." << std::endl;
    FileContent content = parse_csv_file(argv[i]);
    show_file_content(content);
  }
  return 0;
}
