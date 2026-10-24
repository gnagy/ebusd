/*
 * ebusd - daemon for communication with eBUS heating systems.
 * Copyright (C) 2016-2026 John Baier <ebusd@ebusd.eu>
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

#include "lib/utils/rotatefile.h"
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include "lib/utils/clock.h"

namespace ebusd {

using std::streamsize;

RotateFile::~RotateFile() {
  if (m_stream) {
    fclose(m_stream);
    m_stream = nullptr;
  }
}

bool RotateFile::setEnabled(bool enabled) {
  if (enabled == m_enabled) {
    return false;
  }
  m_enabled = enabled;
  if (m_stream) {
    fclose(m_stream);
    m_stream = nullptr;
  }
  if (enabled) {
    struct timespec ts;
    getTime(&ts);
    open(ts.tv_sec);
#ifdef FORWARD_RAW_TTY
    if (!m_textMode && m_stream && isatty(fileno(m_stream)) == 1) {
      int fd = fileno(m_stream);
      struct termios newSettings;
      memset(&newSettings, 0, sizeof(newSettings));

      cfsetspeed(&newSettings, B2400);
      newSettings.c_cflag |= (CS8 | CLOCAL);
      newSettings.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // non-canonical mode
      newSettings.c_iflag |= IGNPAR;  // ignore parity errors
      newSettings.c_oflag &= ~OPOST;

      // activate new settings of serial device
      tcsetattr(fd, TCSANOW, &newSettings);
    }
#endif
  }
  return true;
}

void RotateFile::getTime(struct timespec* ts) const {
  clockGettime(ts);
}

string RotateFile::expandName(time_t now) const {
  struct tm td;
  localtime_r(&now, &td);
  char name[1024];
  size_t len = strftime(name, sizeof(name), m_fileName.c_str(), &td);
  return len > 0 ? string(name, len) : m_fileName;
}

void RotateFile::open(time_t now) {
  m_currentName = m_dated ? expandName(now) : m_fileName;
  m_checkedTime = now;
  // append instead of truncating, so that a restart keeps what was already written
  m_stream = fopen(m_currentName.c_str(), m_textMode ? "a" : "ab");
  m_fileSize = 0;
  m_flushSize = 0;
  struct stat st;
  if (m_stream && fstat(fileno(m_stream), &st) == 0 && S_ISREG(st.st_mode)) {
    m_fileSize = (uint64_t)st.st_size;
  }
}

void RotateFile::write(const unsigned char* value, const size_t size, const bool received, const bool bytes) {
  if (!m_enabled) {
    return;
  }
  struct timespec ts = {0, 0};
  if (m_textMode || m_dated) {
    getTime(&ts);
  }
  if (m_dated && ts.tv_sec != m_checkedTime) {
    m_checkedTime = ts.tv_sec;
    if (expandName(ts.tv_sec) != m_currentName) {
      if (m_stream) {
        fclose(m_stream);
        m_stream = nullptr;
      }
      open(ts.tv_sec);
    }
  }
  if (!m_stream) {
    return;
  }
  if (m_textMode) {
    struct tm td;
    localtime_r(&ts.tv_sec, &td);
    fprintf(m_stream, "%04d-%02d-%02d %02d:%02d:%02d.%03ld ",
      td.tm_year+1900, td.tm_mon+1, td.tm_mday,
      td.tm_hour, td.tm_min, td.tm_sec, ts.tv_nsec/1000000);
    if (bytes) {
      fprintf(m_stream, received ? "<" : ">");
      for (unsigned int pos = 0; pos < size; pos++) {
        fprintf(m_stream, "%2.2x ", value[pos]);
      }
      m_fileSize += 25+3*size+1;
    } else {
      fwrite(value, 1, size, m_stream);
      m_fileSize += 24+size+1;  // timestamp prefix, message, newline
    }
    fprintf(m_stream, "\n");
    fflush(m_stream);
  } else {
    fwrite(value, (streamsize)size, 1, m_stream);
    m_fileSize += size;
    m_flushSize += size;
    if (m_flushSize >= m_flushBuffer) {
      fflush(m_stream);
      m_flushSize = 0;
    }
  }
  if (m_maxSize > 0 && m_fileSize >= m_maxSize * 1024LL) {
    string oldfile = m_currentName+".old";
    if (rename(m_currentName.c_str(), oldfile.c_str()) == 0) {
      fclose(m_stream);
      m_stream = fopen(m_currentName.c_str(), m_textMode ? "w" : "wb");
      m_fileSize = 0;
    }
  }
}

}  // namespace ebusd
