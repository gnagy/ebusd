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
#include <unistd.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <string>
#include <vector>
#include "lib/ebus/device_trans.h"
#include "lib/ebus/protocol_direct.h"
#include "lib/ebus/transport.h"
#include "lib/utils/log.h"

using namespace std;
using namespace ebusd;

static bool error = false;

/** The CRC over the bytes as on the wire. */
static symbol_t crc(const vector<symbol_t>& bytes) {
  symbol_t value = 0;
  for (symbol_t byte : bytes) {
    SymbolString::updateCrc(byte, &value);
  }
  return value;
}

static vector<symbol_t> hexBytes(const string& hex) {
  vector<symbol_t> bytes;
  for (size_t pos = 0; pos + 1 < hex.size(); pos += 2) {
    bytes.push_back((symbol_t)strtoul(hex.substr(pos, 2).c_str(), nullptr, 16));
  }
  return bytes;
}

/** The bytes of @a hex followed by their CRC. */
static vector<symbol_t> withCrc(const string& hex) {
  vector<symbol_t> bytes = hexBytes(hex);
  bytes.push_back(crc(bytes));
  return bytes;
}

static vector<symbol_t> join(std::initializer_list<vector<symbol_t>> parts) {
  vector<symbol_t> bytes;
  for (const auto& part : parts) {
    bytes.insert(bytes.end(), part.begin(), part.end());
  }
  return bytes;
}

/**
 * A bus on a @a Transport: plays scripted bytes, echoes every written byte as the bus does, answers a written
 * sequence with scripted bytes, and generates an AUTO-SYN when idle.
 */
class FakeBus : public Transport {
 public:
  FakeBus() : Transport("fake", 0) {}

  string getTransportInfo() const override { return "fake"; }
  result_t open() override { return RESULT_OK; }
  void close() override {}
  bool isValid() override { return true; }

  result_t write(const uint8_t* data, size_t len) override {
    lock_guard<mutex> lock(m_mutex);
    for (size_t pos = 0; pos < len; pos++) {
      m_written.push_back(data[pos]);
      bool replaced = false;
      for (auto it = m_collisions.begin(); it != m_collisions.end(); ++it) {
        if (it->first == data[pos]) {  // another master wins the arbitration: its bytes instead of the echo
          m_wire.insert(m_wire.end(), it->second.begin(), it->second.end());
          m_collisions.erase(it);
          replaced = true;
          break;
        }
      }
      if (!replaced) {
        m_wire.push_back(data[pos]);
      }
      for (auto it = m_answers.begin(); it != m_answers.end(); ++it) {
        const vector<symbol_t>& trigger = it->first;
        if (m_written.size() >= trigger.size()
            && equal(trigger.begin(), trigger.end(), m_written.end() - (ptrdiff_t)trigger.size())) {
          m_wire.insert(m_wire.end(), it->second.begin(), it->second.end());
          m_answers.erase(it);
          break;
        }
      }
    }
    return RESULT_OK;
  }

  result_t read(unsigned int timeout, const uint8_t** data, size_t* len) override {
    {
      lock_guard<mutex> lock(m_mutex);
      if (!m_wire.empty()) {
        m_buffer.assign(m_wire.begin(), m_wire.end());
        *data = m_buffer.data();
        *len = m_buffer.size();
        return RESULT_OK;
      }
    }
    usleep(5000);  // idle bus: AUTO-SYN
    lock_guard<mutex> lock(m_mutex);
    m_wire.push_back(SYN);
    m_buffer.assign(m_wire.begin(), m_wire.end());
    *data = m_buffer.data();
    *len = m_buffer.size();
    return RESULT_OK;
  }

  void readConsumed(size_t len) override {
    lock_guard<mutex> lock(m_mutex);
    m_wire.erase(m_wire.begin(), m_wire.begin() + (ptrdiff_t)len);
  }

  /** Put bytes on the wire, each telegram followed by a SYN. */
  void play(const vector<symbol_t>& bytes) {
    lock_guard<mutex> lock(m_mutex);
    m_wire.insert(m_wire.end(), bytes.begin(), bytes.end());
    m_wire.push_back(SYN);
  }

  /** Answer the bytes written by ebusd ending in @a trigger with @a answer. */
  void answer(const vector<symbol_t>& trigger, const vector<symbol_t>& answer) {
    lock_guard<mutex> lock(m_mutex);
    m_answers.emplace_back(trigger, answer);
  }

  /** Let another master win the arbitration against ebusd's @a address with @a bytes. */
  void collide(symbol_t address, const vector<symbol_t>& bytes) {
    lock_guard<mutex> lock(m_mutex);
    vector<symbol_t> withSyn = bytes;
    withSyn.push_back(SYN);
    m_collisions.emplace_back(address, withSyn);
  }

  bool drained() {
    lock_guard<mutex> lock(m_mutex);
    for (symbol_t byte : m_wire) {
      if (byte != SYN) {
        return false;
      }
    }
    return true;
  }

 protected:
  result_t openInternal() override { return RESULT_OK; }

 private:
  mutex m_mutex;
  deque<symbol_t> m_wire;
  vector<symbol_t> m_buffer;
  vector<symbol_t> m_written;
  vector<pair<vector<symbol_t>, vector<symbol_t>>> m_answers;
  vector<pair<symbol_t, vector<symbol_t>>> m_collisions;
};

/** A listener naming the message of every completed telegram with primary command 07 for the m= field. */
class TestListener : public ProtocolListener {
 public:
  ProtocolHandler* m_handler = nullptr;
  void notifyProtocolStatus(ProtocolState state, result_t result) override {}
  void notifyProtocolSeenAddress(symbol_t address) override {}
  void notifyProtocolMessage(MessageDirection direction, const MasterSymbolString& master,
      const SlaveSymbolString& slave) override {
    if (master[2] == 0x07) {
      m_handler->setTelegramMatch("test.pb07");
    }
  }
};

static string hexStr(const vector<symbol_t>& bytes) {
  string str;
  char buf[3];
  for (symbol_t byte : bytes) {
    snprintf(buf, sizeof(buf), "%2.2x", byte);
    str += buf;
  }
  return str;
}

static void waitDrained(FakeBus* bus) {
  for (int i = 0; i < 400 && !bus->drained(); i++) {
    usleep(5000);
  }
  usleep(50000);
}

int main() {
  setenv("TZ", "UTC", 1);
  tzset();
  setFacilitiesLogLevel(1 << lf_COUNT, ll_error);
  char dirTemplate[] = "/tmp/test_protocol.XXXXXX";
  if (!mkdtemp(dirTemplate)) {
    cout << "unable to create temp dir" << endl;
    return 1;
  }
  string dir = dirTemplate;
  string logName = dir + "/telegrams.log";

  ebus_protocol_config_t config = {
    .device = "fake",
    .noDeviceCheck = true,
    .readOnly = false,
    .extraLatency = 0,
    .ownAddress = 0x31,
    .answer = false,
    .busLostRetries = 0,
    .failedSendRetries = 0,
    .busAcquireTimeout = 50,
    .slaveRecvTimeout = 50,
    .lockCount = 1,
    .generateSyn = false,
    .initialSend = false,
  };
  auto bus = new FakeBus();
  TestListener listener;
  auto handler = new DirectProtocolHandler(config, new PlainDevice(bus), &listener);
  listener.m_handler = handler;
  handler->setLogTelegramFile(logName.c_str(), 0);
  handler->start("test");
  usleep(50000);

  vector<string> expected;
  // broadcast, OK, with a matching message
  vector<symbol_t> bc = withCrc("10fe07000105");
  bus->play(bc);
  expected.push_back("<" + hexStr(bc) + " m=test.pb07");
  // broadcast with a CRC error
  vector<symbol_t> bcBad = hexBytes("10fe0800010500");
  bus->play(bcBad);
  expected.push_back("<" + hexStr(bcBad) + " r=crc s=recvCmdCrc");
  // master-master, OK
  vector<symbol_t> mm = join({withCrc("1030b509010d"), {ACK}});
  bus->play(mm);
  expected.push_back("<" + hexStr(mm));
  // master-slave, OK
  vector<symbol_t> cmd = withCrc("1015b509010d");
  vector<symbol_t> res = withCrc("021122");
  vector<symbol_t> ms = join({cmd, {ACK}, res, {ACK}});
  bus->play(ms);
  expected.push_back("<" + hexStr(ms));
  // master-slave with a CRC error on the master part, NAK, repeated OK
  vector<symbol_t> cmdBad = hexBytes("1015b509010d00");
  vector<symbol_t> msRepeated = join({cmdBad, {NAK}, cmd, {ACK}, res, {ACK}});
  bus->play(msRepeated);
  expected.push_back("<" + hexStr(msRepeated) + " n=1");
  // master-slave with a CRC error on the slave part twice
  vector<symbol_t> resBad = hexBytes("02112200");
  vector<symbol_t> msFailed = join({cmd, {ACK}, resBad, {NAK}, resBad, {NAK}});
  bus->play(msFailed);
  expected.push_back("<" + hexStr(msFailed) + " r=crc s=recvResCrc n=1");
  // master-slave cut short by a SYN while waiting for the slave
  vector<symbol_t> msCut = join({cmd, {ACK}});
  bus->play(msCut);
  expected.push_back("<" + hexStr(msCut) + " r=syn s=recvRes");
  // broadcast with an escaped data byte, logged as on the wire
  vector<symbol_t> bcEsc = withCrc("10fe07000201a900");
  bus->play(bcEsc);
  expected.push_back("<" + hexStr(bcEsc) + " m=test.pb07");
  waitDrained(bus);

  // own master-slave telegram, answered
  vector<symbol_t> own = withCrc("3115b509010d");
  vector<symbol_t> ownRes = withCrc("023344");
  bus->answer(own, join({{ACK}, ownRes}));
  MasterSymbolString master;
  master.parseHex("3115b509010d");
  SlaveSymbolString slave;
  result_t result = handler->sendAndWait(master, &slave);
  waitDrained(bus);
  if (result != RESULT_OK) {
    cout << "  own send: " << getResultCode(result) << endl;
    error = true;
  }
  expected.push_back(">" + hexStr(own) + "<" + hexStr({ACK}) + hexStr(ownRes) + ">" + hexStr({ACK}) + " a=won");

  // own telegram losing the arbitration against a broadcast of master 10
  bus->collide(0x31, bc);
  result = handler->sendAndWait(master, &slave);
  waitDrained(bus);
  if (result != RESULT_ERR_BUS_LOST) {
    cout << "  lost arbitration: " << getResultCode(result) << endl;
    error = true;
  }
  expected.push_back(">31<" + hexStr(bc) + " a=lost m=test.pb07");

  handler->stop();
  handler->join();
  delete handler;

  // compare without the timestamp prefix and the idle gap, which depend on timing
  ifstream in(logName.c_str());
  vector<string> lines;
  string line;
  regex gap(" g=[0-9]+");
  while (getline(in, line)) {
    if (line.size() > 24) {
      lines.push_back(regex_replace(line.substr(24), gap, ""));
    }
  }
  size_t count = max(lines.size(), expected.size());
  for (size_t pos = 0; pos < count; pos++) {
    string got = pos < lines.size() ? lines[pos] : "(none)";
    string want = pos < expected.size() ? expected[pos] : "(none)";
    if (got == want) {
      cout << "  telegram " << pos << " >" << got << "< OK" << endl;
    } else {
      cout << "  telegram " << pos << " error: got >" << got << "<, expected >" << want << "<" << endl;
      error = true;
    }
  }
  if (system(("rm -rf " + dir).c_str()) != 0) {
    cout << "unable to remove " << dir << endl;
  }
  return error ? 1 : 0;
}
