#pragma once

// Zuständig für SD-Versorgung, Initialisierung und Dateiöffnen auf dieser PCB.
// Vor end() müssen die nutzenden Dienste ihre Dateien geschlossen haben.

#include <SdFat.h>

class SdService {
public:
  bool begin();
  void end();
  bool openNextLogFile(FsFile& file, char* fileName, size_t fileNameSize);
  bool openRoot(FsFile& directory);
  bool openRead(FsFile& file, const char* name);
  const char* lastError() const;

private:
  SdFat sd_;
  const char* lastError_ = "not initialized";
  bool initialized_ = false;

  void configurePins();
};
