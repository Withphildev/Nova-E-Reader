#include "CbzArchive.h"
#include <ZipFile.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

CbzArchive::CbzArchive() = default;
CbzArchive::~CbzArchive() = default;

bool CbzArchive::open(const std::string& cbzPath) {
  filePath = cbzPath;
  pages.clear();

  zip = std::make_unique<ZipFile>(filePath);
  if (!zip->open()) {
    LOG_ERR("CBZ", "Failed to open ZIP archive: %s", filePath.c_str());
    zip.reset();
    return false;
  }

  // Enumerate all files and find images
  zip->enumerateFilePaths([this](std::string_view name) {
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

  zip->loadAllFileStatSlims();

  // Sort files alphabetically
  FsHelpers::sortFileList(pages);

  LOG_INF("CBZ", "Loaded archive with %d pages", (int)pages.size());
  return !pages.empty();
}

void CbzArchive::close() {
  pages.clear();
  if (zip) {
    zip->close();
    zip.reset();
  }
}

bool CbzArchive::extractPageToTempFile(size_t index, const std::string& tempPath) {
  if (index >= pages.size() || !zip) return false;
  
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

  // Extracted block-by-block using 16KB chunk size for much faster decompression
  bool success = zip->readFileToStream(internalPath.c_str(), outFile, 16384);
  outFile.close();

  if (!success) {
    LOG_ERR("CBZ", "Failed to extract file %s to temp file", internalPath.c_str());
    Storage.remove(tempPath.c_str());
    return false;
  }

  return true;
}
