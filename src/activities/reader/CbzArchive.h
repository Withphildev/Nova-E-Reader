#pragma once
#include <string>
#include <vector>

class CbzArchive {
 public:
  CbzArchive() = default;
  ~CbzArchive() = default;

  bool open(const std::string& cbzPath);
  void close();

  size_t getPageCount() const { return pages.size(); }
  std::string getPagePath(size_t index) const {
    if (index >= pages.size()) return "";
    return pages[index];
  }

  bool extractPageToTempFile(size_t index, const std::string& tempPath);

 private:
  std::string filePath;
  std::vector<std::string> pages;
};
