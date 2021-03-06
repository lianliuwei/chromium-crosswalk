// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/test/chromedriver/chrome/adb_impl.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop.h"
#include "base/message_loop/message_loop_proxy.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_tokenizer.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/time.h"
#include "chrome/test/chromedriver/chrome/log.h"
#include "chrome/test/chromedriver/chrome/status.h"
#include "chrome/test/chromedriver/net/adb_client_socket.h"

namespace {

// This class is bound in the callback to AdbQuery and isn't freed until the
// callback is run, even if the function that creates the buffer times out.
class ResponseBuffer : public base::RefCountedThreadSafe<ResponseBuffer> {
 public:
  ResponseBuffer() : ready_(true, false) {}

  void OnResponse(int result, const std::string& response) {
    response_ = response;
    result_ = result;
    ready_.Signal();
  }

  Status GetResponse(
      std::string* response, const base::TimeDelta& timeout) {
    base::TimeTicks deadline = base::TimeTicks::Now() + timeout;
    while (!ready_.IsSignaled()) {
      base::TimeDelta delta = deadline - base::TimeTicks::Now();
      if (delta <= base::TimeDelta())
        return Status(kTimeout, base::StringPrintf(
            "Adb command timed out after %d seconds",
            static_cast<int>(timeout.InSeconds())));
      ready_.TimedWait(timeout);
    }
    if (result_ < 0)
      return Status(kUnknownError,
          "Failed to run adb command, is the adb server running?");
    *response = response_;
    return Status(kOk);
  }

 private:
  friend class base::RefCountedThreadSafe<ResponseBuffer>;
  ~ResponseBuffer() {}

  std::string response_;
  int result_;
  base::WaitableEvent ready_;
};

void ExecuteCommandOnIOThread(
    const std::string& command, scoped_refptr<ResponseBuffer> response_buffer) {
  CHECK(base::MessageLoop::current()->IsType(base::MessageLoop::TYPE_IO));
  AdbClientSocket::AdbQuery(5037, command,
      base::Bind(&ResponseBuffer::OnResponse, response_buffer));
}

}  // namespace

AdbImpl::AdbImpl(
    const scoped_refptr<base::MessageLoopProxy>& io_message_loop_proxy,
    Log* log)
    : io_message_loop_proxy_(io_message_loop_proxy), log_(log) {
  CHECK(io_message_loop_proxy_.get());
}

AdbImpl::~AdbImpl() {}

Status AdbImpl::GetDevices(std::vector<std::string>* devices) {
  std::string response;
  Status status = ExecuteCommand("host:devices", &response);
  if (!status.IsOk())
    return status;
  base::StringTokenizer lines(response, "\n");
  while (lines.GetNext()) {
    std::vector<std::string> fields;
    base::SplitStringAlongWhitespace(lines.token(), &fields);
    if (fields.size() == 2 && fields[1] == "device") {
      devices->push_back(fields[0]);
    }
  }
  return Status(kOk);
}

Status AdbImpl::ForwardPort(
    const std::string& device_serial, int local_port,
    const std::string& remote_abstract) {
  std::string response;
  Status status = ExecuteHostCommand(
      device_serial,
      "forward:tcp:" + base::IntToString(local_port) + ";localabstract:" +
          remote_abstract,
      &response);
  if (!status.IsOk())
    return status;
  if (response == "OKAY")
    return Status(kOk);
  return Status(kUnknownError, "Failed to forward ports to device " +
                device_serial + ": " + response);
}

Status AdbImpl::SetChromeArgs(const std::string& device_serial,
                              const std::string& args) {
  std::string response;
  Status status = ExecuteHostShellCommand(
      device_serial,
      "echo chrome " + args + "> /data/local/chrome-command-line; echo $?",
      &response);
  if (!status.IsOk())
    return status;
  if (response.find("0") == std::string::npos)
    return Status(kUnknownError, "Failed to set Chrome flags on device " +
                  device_serial);
  return Status(kOk);
}

Status AdbImpl::CheckAppInstalled(
    const std::string& device_serial, const std::string& package) {
  std::string response;
  std::string command = "pm path " + package;
  Status status = ExecuteHostShellCommand(device_serial, command, &response);
  if (!status.IsOk())
    return status;
  if (response.find("package") == std::string::npos)
    return Status(kUnknownError, package + " is not installed on device " +
                  device_serial);
  return Status(kOk);
}

Status AdbImpl::ClearAppData(
    const std::string& device_serial, const std::string& package) {
  std::string response;
  std::string command = "pm clear " + package;
  Status status = ExecuteHostShellCommand(device_serial, command, &response);
  if (!status.IsOk())
    return status;
  if (response.find("Success") == std::string::npos)
    return Status(kUnknownError, "Failed to clear data for " + package +
                  " on device " + device_serial + ": " + response);
  return Status(kOk);
}

Status AdbImpl::Launch(
    const std::string& device_serial, const std::string& package,
    const std::string& activity) {
  std::string response;
  Status status = ExecuteHostShellCommand(
      device_serial,
      "am start -a android.intent.action.VIEW -S -W -n " +
          package + "/" + activity + " -d \"data:text/html;charset=utf-8,\"",
      &response);
  if (!status.IsOk())
    return status;
  if (response.find("Complete") == std::string::npos)
    return Status(kUnknownError,
                  "Failed to start " + package + " on device " + device_serial +
                  ": " + response);
  return Status(kOk);
}

Status AdbImpl::ForceStop(
    const std::string& device_serial, const std::string& package) {
  std::string response;
  return ExecuteHostShellCommand(
      device_serial, "am force-stop " + package, &response);
}

Status AdbImpl::ExecuteCommand(
    const std::string& command, std::string* response) {
  scoped_refptr<ResponseBuffer> response_buffer = new ResponseBuffer;
  log_->AddEntry(Log::kDebug, "Sending adb command: " + command);
  io_message_loop_proxy_->PostTask(
      FROM_HERE,
      base::Bind(&ExecuteCommandOnIOThread, command, response_buffer));
  Status status = response_buffer->GetResponse(
      response, base::TimeDelta::FromSeconds(30));
  if (status.IsOk())
    log_->AddEntry(Log::kDebug, "Received adb response: " + *response);
  return status;
}

Status AdbImpl::ExecuteHostCommand(
    const std::string& device_serial,
    const std::string& host_command, std::string* response) {
  return ExecuteCommand(
      "host-serial:" + device_serial + ":" + host_command, response);
}

Status AdbImpl::ExecuteHostShellCommand(
    const std::string& device_serial,
    const std::string& shell_command,
    std::string* response) {
  return ExecuteCommand(
      "host:transport:" + device_serial + "|shell:" + shell_command,
      response);
}

