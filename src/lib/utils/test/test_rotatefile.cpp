/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2026 John Baier <ebusd@ebusd.eu>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include "lib/utils/rotatefile.h"

using namespace std;
using namespace ebusd;

static bool error = false;

/** A @a RotateFile with a settable clock. */
class TestRotateFile : public RotateFile {
 public:
  TestRotateFile(const string fileName, const unsigned int maxSize, const bool textMode)
    : RotateFile(fileName, maxSize, textMode) {}

  time_t m_now = 1767225599;  // 2025-12-31 23:59:59 UTC

 protected:
  void getTime(struct timespec* ts) const override {
    ts->tv_sec = m_now;
    ts->tv_nsec = 0;
  }
};

static string readFile(const string& name) {
  ifstream in(name.c_str(), ios::binary);
  stringstream str;
  str << in.rdbuf();
  return str.str();
}

static int64_t fileSize(const string& name) {
  struct stat st;
  return stat(name.c_str(), &st) == 0 ? st.st_size : -1;
}

static int countLines(const string& text) {
  int lines = 0;
  for (char ch : text) {
    if (ch == '\n') {
      lines++;
    }
  }
  return lines;
}

static void verify(const string& what, bool ok, const string& detail = "") {
  if (ok) {
    cout << "  " << what << " OK" << endl;
  } else {
    cout << "  " << what << " error" << (detail.empty() ? "" : ": " + detail) << endl;
    error = true;
  }
}

static void writeLine(RotateFile* file, const string& line) {
  file->write(reinterpret_cast<const unsigned char*>(line.c_str()), line.size(), true, false);
}

int main() {
  setenv("TZ", "UTC", 1);
  tzset();
  char dirTemplate[] = "/tmp/test_rotatefile.XXXXXX";
  if (!mkdtemp(dirTemplate)) {
    cout << "unable to create temp dir" << endl;
    return 1;
  }
  string dir = dirTemplate;

  // a text file keeps its content when writing is disabled and enabled again
  string name = dir + "/raw.log";
  {
    TestRotateFile file(name, 100, true);
    file.setEnabled(true);
    writeLine(&file, "<10fe0700");
    file.setEnabled(false);
    file.setEnabled(true);
    writeLine(&file, "<10fe0701");
  }
  string text = readFile(name);
  verify("text append on re-enable", countLines(text) == 2 && text.find("<10fe0700") != string::npos
      && text.find("<10fe0701") != string::npos, text);

  // a new instance on the same file appends as well (ebusd restart)
  {
    TestRotateFile file(name, 100, true);
    file.setEnabled(true);
    writeLine(&file, "<10fe0702");
  }
  text = readFile(name);
  verify("text append on restart", countLines(text) == 3, text);
  verify("text line format", text.compare(0, 34, "2025-12-31 23:59:59.000 <10fe0700\n") == 0, text.substr(0, 34));

  // a binary file appends too
  string dumpName = dir + "/dump.bin";
  const unsigned char bytes[] = {0xaa, 0x10, 0xfe, 0xaa};
  for (int i = 0; i < 2; i++) {
    TestRotateFile file(dumpName, 100, false);
    file.setEnabled(true);
    file.write(bytes, sizeof(bytes));
  }
  verify("binary append on restart", fileSize(dumpName) == 2 * sizeof(bytes), to_string(fileSize(dumpName)));

  // the size limit counts the timestamp prefix: 1 kB of 50 byte lines rotates after 21 lines (21*50 >= 1024)
  string sizeName = dir + "/size.log";
  {
    TestRotateFile file(sizeName, 1, true);
    file.setEnabled(true);
    string message(25, 'x');  // 24 prefix + 25 + newline = 50 bytes per line
    for (int i = 0; i < 21; i++) {
      writeLine(&file, message);
    }
  }
  verify("size counts the prefix", fileSize(sizeName + ".old") == 21 * 50 && fileSize(sizeName) == 0,
      to_string(fileSize(sizeName + ".old")) + "/" + to_string(fileSize(sizeName)));

  // the size limit takes an existing file into account
  string resumeName = dir + "/resume.log";
  {
    TestRotateFile file(resumeName, 1, true);
    file.setEnabled(true);
    string message(25, 'x');
    for (int i = 0; i < 20; i++) {
      writeLine(&file, message);
    }
  }
  {
    TestRotateFile file(resumeName, 1, true);
    file.setEnabled(true);
    writeLine(&file, string(25, 'x'));
  }
  verify("size resumes after restart", fileSize(resumeName + ".old") == 21 * 50,
      to_string(fileSize(resumeName + ".old")));

  // size 0 never rotates
  string unlimitedName = dir + "/unlimited.log";
  {
    TestRotateFile file(unlimitedName, 0, true);
    file.setEnabled(true);
    for (int i = 0; i < 100; i++) {
      writeLine(&file, string(25, 'x'));
    }
  }
  verify("size 0 is unlimited", fileSize(unlimitedName) == 100 * 50 && fileSize(unlimitedName + ".old") == -1,
      to_string(fileSize(unlimitedName)));

  // a dated name moves on to a new file when the expansion changes
  string datedName = dir + "/raw-%Y%m%d.log";
  {
    TestRotateFile file(datedName, 0, true);
    file.setEnabled(true);
    verify("dated name expanded", file.getCurrentName() == dir + "/raw-20251231.log", file.getCurrentName());
    writeLine(&file, "<10fe0700");
    file.m_now += 1;  // 2026-01-01 00:00:00
    writeLine(&file, "<10fe0701");
    writeLine(&file, "<10fe0702");
    verify("dated name switched", file.getCurrentName() == dir + "/raw-20260101.log", file.getCurrentName());
  }
  string day1 = readFile(dir + "/raw-20251231.log");
  string day2 = readFile(dir + "/raw-20260101.log");
  verify("dated files split at midnight", countLines(day1) == 1 && countLines(day2) == 2
      && day2.compare(0, 24, "2026-01-01 00:00:00.000 ") == 0, day1 + "|" + day2);

  // a dated name appends to the file of the day on restart
  {
    TestRotateFile file(datedName, 0, true);
    file.m_now = 1767225600 + 3600;  // 2026-01-01 01:00:00
    file.setEnabled(true);
    writeLine(&file, "<10fe0703");
  }
  verify("dated append on restart", countLines(readFile(dir + "/raw-20260101.log")) == 3);

  if (system(("rm -rf " + dir).c_str()) != 0) {
    cout << "unable to remove " << dir << endl;
  }
  return error ? 1 : 0;
}
