/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_VIDEO_CODING_TEST_TEST_UTIL_H_
#define WEBRTC_MODULES_VIDEO_CODING_TEST_TEST_UTIL_H_

/*
 * General declarations used through out VCM offline tests.
 */

#include <string>

#include "base/constructormagic.h"
#include "interface/module_common_types.h"
#include "video_coding/main/interface/video_coding.h"
#include "system_wrappers/interface/event_wrapper.h"

enum { kMaxNackListSize = 250 };
enum { kMaxPacketAgeToNack = 450 };

class NullEvent : public webrtc::EventWrapper {
 public:
  virtual ~NullEvent() {}

  virtual bool Set() { return true; }

  virtual bool Reset() { return true; }

  virtual webrtc::EventTypeWrapper Wait(unsigned long max_time) {
    return webrtc::kEventTimeout;
  }

  virtual bool StartTimer(bool periodic, unsigned long time) { return true; }

  virtual bool StopTimer() { return true; }
};

class NullEventFactory : public webrtc::EventFactory {
 public:
  virtual ~NullEventFactory() {}

  virtual webrtc::EventWrapper* CreateEvent() {
    return new NullEvent;
  }
};

class FileOutputFrameReceiver : public webrtc::VCMReceiveCallback {
 public:
  FileOutputFrameReceiver(const std::string& base_out_filename, uint32_t ssrc);
  virtual ~FileOutputFrameReceiver();

  // VCMReceiveCallback
  virtual int32_t FrameToRender(webrtc::VideoFrame& video_frame);

 private:
  std::string out_filename_;
  FILE* out_file_;
  FILE* timing_file_;
  int width_;
  int height_;
  int count_;

  RTC_DISALLOW_IMPLICIT_CONSTRUCTORS(FileOutputFrameReceiver);
};

class CmdArgs {
 public:
  CmdArgs();

  std::string codecName;
  webrtc::VideoCodecType codecType;
  int width;
  int height;
  int rtt;
  std::string inputFile;
  std::string outputFile;
};

#endif
