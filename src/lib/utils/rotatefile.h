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

#ifndef LIB_UTILS_ROTATEFILE_H_
#define LIB_UTILS_ROTATEFILE_H_

#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <iostream>
#include <fstream>
#include <string>

namespace ebusd {

/** \file lib/utils/rotatefile.h
 * Helpers for writing to rotating files.
 */

using std::string;

/**
 * Helper class for writing to a rotating file with maximum size.
 * An existing file is appended to. A file name containing strftime() conversions (e.g. "raw-%Y%m%d.log") is
 * expanded with the local time, and writing moves on to a new file whenever the expansion changes.
 */
class RotateFile {
 public:
  /**
   * Construct a new instance.
   * @param fileName the name of the file write to, optionally with strftime() conversions.
   * @param maxSize the maximum size of the file to write to in kB, or 0 for infinite.
   * @param textMode whether to write each byte with prefixed timestamp and direction as text.
   * @param flushBuffer the size of the flush buffer.
   */
  RotateFile(const string fileName, const unsigned int maxSize, const bool textMode = false,
             const unsigned int flushBuffer = 16)
    : m_enabled(false), m_fileName(fileName), m_dated(fileName.find('%') != string::npos), m_maxSize(maxSize),
      m_textMode(textMode), m_stream(), m_checkedTime(0), m_fileSize(0), m_flushSize(0),
      m_flushBuffer(flushBuffer) {}

  /**
   * Destructor.
   */
  virtual ~RotateFile();

  /**
   * Enable or disable writing to the file.
   * @param enabled @p true to enable writing to the file, @p false to disable it.
   * @return @p true when the state was changed, @p false otherwise.
   */
  bool setEnabled(bool enabled);

  /**
   * Return whether writing to the file is enabled.
   * @return whether writing to the file is enabled.
   */
  bool isEnabled() { return m_enabled; }

  /**
   * Write a number of bytes to the stream.
   * @param value the pointer to the bytes to write.
   * @param size the number of bytes to write.
   * @param received @a true on reception, @a false on sending (only relevant in text mode).
   * @param bytes whether to log single bytes (only relevant in text mode).
   */
  void write(const unsigned char* value, const size_t size, const bool received = true,
      const bool bytes = true);

  /**
   * Return the name of the file currently written to.
   * @return the name of the file currently written to (with strftime() conversions expanded).
   */
  const string& getCurrentName() const { return m_currentName; }


 protected:
  /**
   * Get the current time.
   * @param ts the @a timespec to fill.
   */
  virtual void getTime(struct timespec* ts) const;


 private:
  /**
   * Open the file for the specified time, appending to it if it already exists.
   * @param now the current time.
   */
  void open(time_t now);

  /**
   * Expand the strftime() conversions in the file name.
   * @param now the time to expand with.
   * @return the expanded file name.
   */
  string expandName(time_t now) const;

  /** whether writing to the file is enabled. */
  bool m_enabled;

  /** the name of the file write to, optionally with strftime() conversions. */
  const string m_fileName;

  /** whether @a m_fileName contains strftime() conversions. */
  const bool m_dated;

  /** the maximum size of @a m_file in kB, or 0 for infinite. */
  const unsigned int m_maxSize;

  /** whether to write each byte with prefixed timestamp and direction as text. */
  const bool m_textMode;

  /** the @a FILE to writing to. */
  FILE* m_stream;

  /** the name of the file currently written to. */
  string m_currentName;

  /** the second in which @a m_currentName was last checked against the time. */
  time_t m_checkedTime;

  /** the number of bytes in @a m_file. */
  uint64_t m_fileSize;

  /** the number of bytes written to @a m_file since the last flush. */
  uint64_t m_flushSize;

  /** the size of the flush buffer. */
  const unsigned int m_flushBuffer;
};

}  // namespace ebusd

#endif  // LIB_UTILS_ROTATEFILE_H_
