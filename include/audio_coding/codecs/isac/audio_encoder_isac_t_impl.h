/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_MODULES_AUDIO_CODING_CODECS_ISAC_AUDIO_ENCODER_ISAC_T_IMPL_H_
#define WEBRTC_MODULES_AUDIO_CODING_CODECS_ISAC_AUDIO_ENCODER_ISAC_T_IMPL_H_

#include "audio_coding/codecs/isac/main/interface/audio_encoder_isac.h"

#include <algorithm>

#include "base/checks.h"
#include "audio_coding/codecs/isac/main/interface/isac.h"

namespace webrtc {

template <typename T>
typename AudioEncoderIsacT<T>::Config CreateIsacConfig(
    const CodecInst& codec_inst,
    LockedIsacBandwidthInfo* bwinfo) {
  typename AudioEncoderIsacT<T>::Config config;
  config.bwinfo = bwinfo;
  config.payload_type = codec_inst.pltype;
  config.sample_rate_hz = codec_inst.plfreq;
  config.frame_size_ms =
      rtc::CheckedDivExact(1000 * codec_inst.pacsize, config.sample_rate_hz);
  config.adaptive_mode = (codec_inst.rate == -1);
  if (codec_inst.rate != -1)
    config.bit_rate = codec_inst.rate;
  return config;
}

template <typename T>
bool AudioEncoderIsacT<T>::Config::IsOk() const {
  if (max_bit_rate < 32000 && max_bit_rate != -1)
    return false;
  if (max_payload_size_bytes < 120 && max_payload_size_bytes != -1)
    return false;
  if (adaptive_mode && !bwinfo)
    return false;
  switch (sample_rate_hz) {
    case 16000:
      if (max_bit_rate > 53400)
        return false;
      if (max_payload_size_bytes > 400)
        return false;
      return (frame_size_ms == 30 || frame_size_ms == 60) &&
             (bit_rate == 0 || (bit_rate >= 10000 && bit_rate <= 32000));
    case 32000:
    case 48000:
      if (max_bit_rate > 160000)
        return false;
      if (max_payload_size_bytes > 600)
        return false;
      return T::has_swb &&
             (frame_size_ms == 30 &&
              (bit_rate == 0 || (bit_rate >= 10000 && bit_rate <= 56000)));
    default:
      return false;
  }
}

template <typename T>
AudioEncoderIsacT<T>::AudioEncoderIsacT(const Config& config) {
  RecreateEncoderInstance(config);
}

template <typename T>
AudioEncoderIsacT<T>::AudioEncoderIsacT(const CodecInst& codec_inst,
                                        LockedIsacBandwidthInfo* bwinfo)
    : AudioEncoderIsacT(CreateIsacConfig<T>(codec_inst, bwinfo)) {}

template <typename T>
AudioEncoderIsacT<T>::~AudioEncoderIsacT() {
  RTC_CHECK_EQ(0, T::Free(isac_state_));
}

template <typename T>
size_t AudioEncoderIsacT<T>::MaxEncodedBytes() const {
  return kSufficientEncodeBufferSizeBytes;
}

template <typename T>
int AudioEncoderIsacT<T>::SampleRateHz() const {
  return T::EncSampRate(isac_state_);
}

template <typename T>
int AudioEncoderIsacT<T>::NumChannels() const {
  return 1;
}

template <typename T>
size_t AudioEncoderIsacT<T>::Num10MsFramesInNextPacket() const {
  const int samples_in_next_packet = T::GetNewFrameLen(isac_state_);
  return static_cast<size_t>(
      rtc::CheckedDivExact(samples_in_next_packet,
                           rtc::CheckedDivExact(SampleRateHz(), 100)));
}

template <typename T>
size_t AudioEncoderIsacT<T>::Max10MsFramesInAPacket() const {
  return 6;  // iSAC puts at most 60 ms in a packet.
}

template <typename T>
int AudioEncoderIsacT<T>::GetTargetBitrate() const {
  if (config_.adaptive_mode)
    return -1;
  return config_.bit_rate == 0 ? kDefaultBitRate : config_.bit_rate;
}

template <typename T>
AudioEncoder::EncodedInfo AudioEncoderIsacT<T>::EncodeInternal(
    uint32_t rtp_timestamp,
    const int16_t* audio,
    size_t max_encoded_bytes,
    uint8_t* encoded) {
  if (!packet_in_progress_) {
    // Starting a new packet; remember the timestamp for later.
    packet_in_progress_ = true;
    packet_timestamp_ = rtp_timestamp;
  }
  if (bwinfo_) {
    IsacBandwidthInfo bwinfo = bwinfo_->Get();
    T::SetBandwidthInfo(isac_state_, &bwinfo);
  }
  int r = T::Encode(isac_state_, audio, encoded);
  RTC_CHECK_GE(r, 0) << "Encode failed (error code "
                     << T::GetErrorCode(isac_state_) << ")";

  // T::Encode doesn't allow us to tell it the size of the output
  // buffer. All we can do is check for an overrun after the fact.
  RTC_CHECK_LE(static_cast<size_t>(r), max_encoded_bytes);

  if (r == 0)
    return EncodedInfo();

  // Got enough input to produce a packet. Return the saved timestamp from
  // the first chunk of input that went into the packet.
  packet_in_progress_ = false;
  EncodedInfo info;
  info.encoded_bytes = r;
  info.encoded_timestamp = packet_timestamp_;
  info.payload_type = config_.payload_type;
  return info;
}

template <typename T>
void AudioEncoderIsacT<T>::Reset() {
  RecreateEncoderInstance(config_);
}

template <typename T>
void AudioEncoderIsacT<T>::RecreateEncoderInstance(const Config& config) {
  RTC_CHECK(config.IsOk());
  packet_in_progress_ = false;
  bwinfo_ = config.bwinfo;
  if (isac_state_)
    RTC_CHECK_EQ(0, T::Free(isac_state_));
  RTC_CHECK_EQ(0, T::Create(&isac_state_));
  RTC_CHECK_EQ(0, T::EncoderInit(isac_state_, config.adaptive_mode ? 0 : 1));
  RTC_CHECK_EQ(0, T::SetEncSampRate(isac_state_, config.sample_rate_hz));
  const int bit_rate = config.bit_rate == 0 ? kDefaultBitRate : config.bit_rate;
  if (config.adaptive_mode) {
    RTC_CHECK_EQ(0, T::ControlBwe(isac_state_, bit_rate, config.frame_size_ms,
                                  config.enforce_frame_size));
  } else {
    RTC_CHECK_EQ(0, T::Control(isac_state_, bit_rate, config.frame_size_ms));
  }
  if (config.max_payload_size_bytes != -1)
    RTC_CHECK_EQ(
        0, T::SetMaxPayloadSize(isac_state_, config.max_payload_size_bytes));
  if (config.max_bit_rate != -1)
    RTC_CHECK_EQ(0, T::SetMaxRate(isac_state_, config.max_bit_rate));

  // When config.sample_rate_hz is set to 48000 Hz (iSAC-fb), the decoder is
  // still set to 32000 Hz, since there is no full-band mode in the decoder.
  const int decoder_sample_rate_hz = std::min(config.sample_rate_hz, 32000);

  // Set the decoder sample rate even though we just use the encoder. This
  // doesn't appear to be necessary to produce a valid encoding, but without it
  // we get an encoding that isn't bit-for-bit identical with what a combined
  // encoder+decoder object produces.
  RTC_CHECK_EQ(0, T::SetDecSampRate(isac_state_, decoder_sample_rate_hz));

  config_ = config;
}

template <typename T>
AudioDecoderIsacT<T>::AudioDecoderIsacT()
    : AudioDecoderIsacT(nullptr) {}

template <typename T>
AudioDecoderIsacT<T>::AudioDecoderIsacT(LockedIsacBandwidthInfo* bwinfo)
    : bwinfo_(bwinfo), decoder_sample_rate_hz_(-1) {
  RTC_CHECK_EQ(0, T::Create(&isac_state_));
  T::DecoderInit(isac_state_);
  if (bwinfo_) {
    IsacBandwidthInfo bi;
    T::GetBandwidthInfo(isac_state_, &bi);
    bwinfo_->Set(bi);
  }
}

template <typename T>
AudioDecoderIsacT<T>::~AudioDecoderIsacT() {
  RTC_CHECK_EQ(0, T::Free(isac_state_));
}

template <typename T>
int AudioDecoderIsacT<T>::DecodeInternal(const uint8_t* encoded,
                                         size_t encoded_len,
                                         int sample_rate_hz,
                                         int16_t* decoded,
                                         SpeechType* speech_type) {
  // We want to crate the illusion that iSAC supports 48000 Hz decoding, while
  // in fact it outputs 32000 Hz. This is the iSAC fullband mode.
  if (sample_rate_hz == 48000)
    sample_rate_hz = 32000;
  RTC_CHECK(sample_rate_hz == 16000 || sample_rate_hz == 32000)
      << "Unsupported sample rate " << sample_rate_hz;
  if (sample_rate_hz != decoder_sample_rate_hz_) {
    RTC_CHECK_EQ(0, T::SetDecSampRate(isac_state_, sample_rate_hz));
    decoder_sample_rate_hz_ = sample_rate_hz;
  }
  int16_t temp_type = 1;  // Default is speech.
  int ret =
      T::DecodeInternal(isac_state_, encoded, encoded_len, decoded, &temp_type);
  *speech_type = ConvertSpeechType(temp_type);
  return ret;
}

template <typename T>
bool AudioDecoderIsacT<T>::HasDecodePlc() const {
  return false;
}

template <typename T>
size_t AudioDecoderIsacT<T>::DecodePlc(size_t num_frames, int16_t* decoded) {
  return T::DecodePlc(isac_state_, decoded, num_frames);
}

template <typename T>
void AudioDecoderIsacT<T>::Reset() {
  T::DecoderInit(isac_state_);
}

template <typename T>
int AudioDecoderIsacT<T>::IncomingPacket(const uint8_t* payload,
                                         size_t payload_len,
                                         uint16_t rtp_sequence_number,
                                         uint32_t rtp_timestamp,
                                         uint32_t arrival_timestamp) {
  int ret = T::UpdateBwEstimate(
      isac_state_, payload, payload_len,
      rtp_sequence_number, rtp_timestamp, arrival_timestamp);
  if (bwinfo_) {
    IsacBandwidthInfo bwinfo;
    T::GetBandwidthInfo(isac_state_, &bwinfo);
    bwinfo_->Set(bwinfo);
  }
  return ret;
}

template <typename T>
int AudioDecoderIsacT<T>::ErrorCode() {
  return T::GetErrorCode(isac_state_);
}

template <typename T>
size_t AudioDecoderIsacT<T>::Channels() const {
  return 1;
}

}  // namespace webrtc

#endif  // WEBRTC_MODULES_AUDIO_CODING_CODECS_ISAC_AUDIO_ENCODER_ISAC_T_IMPL_H_
