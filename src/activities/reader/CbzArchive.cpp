#include "CbzArchive.h"
#include <ZipFile.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

bool CbzArchive::open(const std::string& cbzPath) {
  filePath = cbzPath;
  pages.clear();

  ZipFile zip(filePath);
  if (!zip.open()) {
    LOG_ERR("CBZ", "Failed to open ZIP archive: %s", filePath.c_str());
    return false;
  }

  // Enumerate all files and find images
  zip.enumerateFilePaths([this](std::string_view name) {
    std::string pathStr(name);
    // Ignore hidden files and MAC metadata directories
    if (pathStr.empty() || pathStr[0] == '.' || pathStr.find("__MACOSX") != std::string::npos) {
      return;
    }
    // Filter to JPEGs, PNGs, and BMPs
    if (FsHelpers::hasJpgExtension(pathStr) ||
        FsHelpers::hasPngExtension(pathStr) ||
        FsHelpers::hasBmpExtension(pathStr)) {
      pages.push_back(pathStr);
    }
  });

  zip.close();

  // Sort files alphabetically
  FsHelpers::sortFileList(pages);

  LOG_INF("CBZ", "Loaded archive with %d pages", (int)pages.size());
  return !pages.empty();
}

void CbzArchive::close() {
  pages.clear();
}

bool CbzArchive::extractPageToTempFile(size_t index, const std::string& tempPath) {
  if (index >= pages.size()) return false;
  
  const std::string& internalPath = pages[index];
  
  // Make sure destination directory exists
  std::string dirPath = FsHelpers::extractFolderPath(tempPath);
  if (!Storage.exists(dirPath.c_str())) {
    Storage.mkdir(dirPath.c_str());
  }

  HalFile outFile;
  if (!Storage.openFileForWrite("CBZ", tempPath, outFile)) {
    LOG_ERR("CBZ", "Failed to open temp file for writing: %s", tempPath.c_str());
    return false;
  }

  ZipFile zip(filePath);
  // Extracted block-by-block using 4KB chunk size to keep heap footprint negligible
  bool success = zip.readFileToStream(internalPath.c_str(), outFile, 4096);
  outFile.close();

  if (!success) {
    LOG_ERR("CBZ", "Failed to extract file %s to temp file", internalPath.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }

  return true;
}
