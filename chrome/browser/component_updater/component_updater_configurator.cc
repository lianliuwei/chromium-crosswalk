// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/component_updater/component_updater_configurator.h"

#include <algorithm>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/metrics/histogram.h"
#include "base/strings/string_util.h"
#include "base/win/windows_version.h"
#include "build/build_config.h"
#include "chrome/browser/component_updater/component_patcher.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/omaha_query_params/omaha_query_params.h"
#include "net/url_request/url_request_context_getter.h"

#if defined(OS_WIN)
#include "chrome/browser/component_updater/component_patcher_win.h"
#endif

namespace {

// Default time constants.
const int kDelayOneMinute = 60;
const int kDelayOneHour = kDelayOneMinute * 60;

// Debug values you can pass to --component-updater-debug=value1,value2.
// Speed up component checking.
const char kSwitchFastUpdate[] = "fast-update";
// Force out-of-process-xml parsing.
const char kSwitchOutOfProcess[] = "out-of-process";
// Add "testrequest=1" parameter to the update check query.
const char kSwitchRequestParam[] = "test-request";
// Disables differential updates.
const char kSwitchDisableDeltaUpdates[] = "disable-delta-updates";

// The urls from which an update manifest can be fetched.
const char* kUrlSources[] = {
  "http://clients2.google.com/service/update2/crx",       // BANDAID
  "http://omaha.google.com/service/update2/crx",          // CWS_PUBLIC
  "http://omaha.sandbox.google.com/service/update2/crx",   // CWS_SANDBOX
};

bool HasSwitchValue(const std::vector<std::string>& vec, const char* test) {
  if (vec.empty())
    return 0;
  return (std::find(vec.begin(), vec.end(), test) != vec.end());
}

}  // namespace

class ChromeConfigurator : public ComponentUpdateService::Configurator {
 public:
  ChromeConfigurator(const CommandLine* cmdline,
                     net::URLRequestContextGetter* url_request_getter);

  virtual ~ChromeConfigurator() {}

  virtual int InitialDelay() OVERRIDE;
  virtual int NextCheckDelay() OVERRIDE;
  virtual int StepDelay() OVERRIDE;
  virtual int MinimumReCheckWait() OVERRIDE;
  virtual int OnDemandDelay() OVERRIDE;
  virtual GURL UpdateUrl(CrxComponent::UrlSource source) OVERRIDE;
  virtual const char* ExtraRequestParams() OVERRIDE;
  virtual size_t UrlSizeLimit() OVERRIDE;
  virtual net::URLRequestContextGetter* RequestContext() OVERRIDE;
  virtual bool InProcess() OVERRIDE;
  virtual void OnEvent(Events event, int val) OVERRIDE;
  virtual ComponentPatcher* CreateComponentPatcher() OVERRIDE;
  virtual bool DeltasEnabled() const OVERRIDE;

 private:
  net::URLRequestContextGetter* url_request_getter_;
  std::string extra_info_;
  bool fast_update_;
  bool out_of_process_;
  bool deltas_enabled_;
};

ChromeConfigurator::ChromeConfigurator(const CommandLine* cmdline,
    net::URLRequestContextGetter* url_request_getter)
      : url_request_getter_(url_request_getter),
        extra_info_(chrome::OmahaQueryParams::Get(
            chrome::OmahaQueryParams::CHROME)),
        fast_update_(false),
        out_of_process_(false),
        deltas_enabled_(false) {
  // Parse comma-delimited debug flags.
  std::vector<std::string> switch_values;
  Tokenize(cmdline->GetSwitchValueASCII(switches::kComponentUpdater),
      ",", &switch_values);
  fast_update_ = HasSwitchValue(switch_values, kSwitchFastUpdate);
  out_of_process_ = HasSwitchValue(switch_values, kSwitchOutOfProcess);
#if defined(OS_WIN)
  deltas_enabled_ = !HasSwitchValue(switch_values, kSwitchDisableDeltaUpdates);
#else
  deltas_enabled_ = false;
#endif

  // Make the extra request params, they are necessary so omaha does
  // not deliver components that are going to be rejected at install time.
#if defined(OS_WIN)
  if (base::win::OSInfo::GetInstance()->wow64_status() ==
      base::win::OSInfo::WOW64_ENABLED)
    extra_info_ += "&wow64=1";
#endif
  if (HasSwitchValue(switch_values, kSwitchRequestParam))
    extra_info_ += "&testrequest=1";
}

int ChromeConfigurator::InitialDelay() {
  return fast_update_ ? 1 : (6 * kDelayOneMinute);
}

int ChromeConfigurator::NextCheckDelay() {
  return fast_update_ ? 3 : (2 * kDelayOneHour);
}

int ChromeConfigurator::StepDelay() {
  return fast_update_ ? 1 : 4;
}

int ChromeConfigurator::MinimumReCheckWait() {
  return fast_update_ ? 30 : (6 * kDelayOneHour);
}

int ChromeConfigurator::OnDemandDelay() {
  return fast_update_ ? 2 : (30 * kDelayOneMinute);
}

GURL ChromeConfigurator::UpdateUrl(CrxComponent::UrlSource source) {
  return GURL(kUrlSources[source]);
}

const char* ChromeConfigurator::ExtraRequestParams() {
  return extra_info_.c_str();
}

size_t ChromeConfigurator::UrlSizeLimit() {
  return 1024ul;
}

net::URLRequestContextGetter* ChromeConfigurator::RequestContext() {
  return url_request_getter_;
}

bool ChromeConfigurator::InProcess() {
  return !out_of_process_;
}

void ChromeConfigurator::OnEvent(Events event, int val) {
  switch (event) {
    case kManifestCheck:
      UMA_HISTOGRAM_ENUMERATION("ComponentUpdater.ManifestCheck", val, 100);
      break;
    case kComponentUpdated:
      UMA_HISTOGRAM_ENUMERATION("ComponentUpdater.ComponentUpdated", val, 100);
      break;
    case kManifestError:
      UMA_HISTOGRAM_COUNTS_100("ComponentUpdater.ManifestError", val);
      break;
    case kNetworkError:
      UMA_HISTOGRAM_ENUMERATION("ComponentUpdater.NetworkError", val, 100);
      break;
    case kUnpackError:
      UMA_HISTOGRAM_ENUMERATION("ComponentUpdater.UnpackError", val, 100);
      break;
    case kInstallerError:
      UMA_HISTOGRAM_ENUMERATION("ComponentUpdater.InstallError", val, 100);
      break;
    default:
      NOTREACHED();
      break;
  }
}

ComponentPatcher* ChromeConfigurator::CreateComponentPatcher() {
#if defined(OS_WIN)
  return new ComponentPatcherWin();
#else
  return new ComponentPatcherCrossPlatform();
#endif
}

bool ChromeConfigurator::DeltasEnabled() const {
  return deltas_enabled_;
}

ComponentUpdateService::Configurator* MakeChromeComponentUpdaterConfigurator(
    const CommandLine* cmdline, net::URLRequestContextGetter* context_getter) {
  return new ChromeConfigurator(cmdline, context_getter);
}
