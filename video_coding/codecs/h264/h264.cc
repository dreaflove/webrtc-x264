/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "video_coding/codecs/h264/include/h264.h"

#if defined(WEBRTC_THIRD_PARTY_H264)
#include "video_coding/codecs/h264/h264_encoder_impl.h"
#include "video_coding/codecs/h264/h264_decoder_impl.h"
#endif
#if defined(WEBRTC_IOS)
#include "video_coding/codecs/h264/h264_video_toolbox_decoder.h"
#include "video_coding/codecs/h264/h264_video_toolbox_encoder.h"
#endif

#include "base/checks.h"
#include "base/logging.h"

namespace webrtc {

// We need this file to be C++ only so it will compile properly for all
// platforms. In order to write ObjC specific implementations we use private
// externs. This function is defined in h264.mm.
#if defined(WEBRTC_IOS)
extern bool IsH264CodecSupportedObjC();
#endif

// If any H.264 codec is supported (iOS HW or OpenH264/FFmpeg).
bool IsH264CodecSupported() {
#if defined(WEBRTC_IOS)
  if (IsH264CodecSupportedObjC()) {
    return true;
  }
#endif
#if defined(WEBRTC_THIRD_PARTY_H264)
  return true;
#else
  return false;
#endif
}

H264Encoder* H264Encoder::Create() {
  RTC_DCHECK(H264Encoder::IsSupported());
#if defined(WEBRTC_IOS) && defined(WEBRTC_VIDEO_TOOLBOX_SUPPORTED)
  if (IsH264CodecSupportedObjC()) {
    LOG(LS_INFO) << "Creating H264VideoToolboxEncoder.";
    return new H264VideoToolboxEncoder();
  }
#endif
#if defined(WEBRTC_THIRD_PARTY_H264)
  LOG(LS_INFO) << "Creating H264EncoderImpl.";
  return new H264EncoderImpl();
#else
  RTC_NOTREACHED();
  return nullptr;
#endif
}

bool H264Encoder::IsSupported() {
  return IsH264CodecSupported();
}

H264Decoder* H264Decoder::Create() {
  RTC_DCHECK(H264Decoder::IsSupported());
#if defined(WEBRTC_IOS) && defined(WEBRTC_VIDEO_TOOLBOX_SUPPORTED)
  if (IsH264CodecSupportedObjC()) {
    LOG(LS_INFO) << "Creating H264VideoToolboxDecoder.";
    return new H264VideoToolboxDecoder();
  }
#endif
#if defined(WEBRTC_THIRD_PARTY_H264)
  LOG(LS_INFO) << "Creating H264DecoderImpl.";
  return new H264DecoderImpl();
#else
  RTC_NOTREACHED();
  return nullptr;
#endif
}

bool H264Decoder::IsSupported() {
  return IsH264CodecSupported();
}

}  // namespace webrtc
