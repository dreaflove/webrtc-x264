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

#include "video_coding/codecs/h264/h264_encoder_impl.h"

#include <limits>

#include "base/checks.h"
#include "base/logging.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "system_wrappers/interface/trace.h"

namespace webrtc {

namespace {

const bool kOpenH264EncoderDetailedLogging = false;

int NumberOfThreads(int width, int height, int number_of_cores) {
  if (width * height >= 1920 * 1080 && number_of_cores > 8) {
    return 8;  // 8 threads for 1080p on high perf machines.
  } else if (width * height > 1280 * 960 && number_of_cores >= 6) {
    return 3;  // 3 threads for 1080p.
  } else if (width * height > 640 * 480 && number_of_cores >= 3) {
    return 2;  // 2 threads for qHD/HD.
  } else {
    return 1;  // 1 thread for VGA or less.
  }
}

}  // namespace


// Helper method used by H264EncoderImpl::Encode.
// Copies the encoded bytes from |info| to |encoded_image| and updates the
// fragmentation information of |frag_header|. The |encoded_image->_buffer| may
// be deleted and reallocated if a bigger buffer is required.
//
// After OpenH264 encoding, the encoded bytes are stored in |info| spread out
// over a number of layers and "NAL units". Each NAL unit is a fragment starting
// with the four-byte start code {0,0,0,1}. All of this data (including the
// start codes) is copied to the |encoded_image->_buffer| and the |frag_header|
// is updated to point to each fragment, with offsets and lengths set as to
// exclude the start codes.

H264EncoderImpl::H264EncoderImpl()
    : encoder_(nullptr),
      encoded_image_callback_(nullptr),
    inited_(false){
}

H264EncoderImpl::~H264EncoderImpl() {
  Release();
}

int32_t H264EncoderImpl::InitEncode(const VideoCodec* inst,
                                    int32_t number_of_cores,
                                    size_t /*max_payload_size*/) {
    
        if (inst == NULL) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        if (inst->maxFramerate < 1) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        // allow zero to represent an unspecified maxBitRate
        if (inst->maxBitrate > 0 && inst->startBitrate > inst->maxBitrate) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        if (inst->width < 1 || inst->height < 1) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        if (number_of_cores < 1) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        
        int ret_val = Release();
        if (ret_val < 0) {
            return ret_val;
        }
        codec_settings_ = *inst;
        /* Get default params for preset/tuning */
        x264_param_t param;
        memset(&param, 0, sizeof(param));
        x264_param_default(&param);
        ret_val = x264_param_default_preset(&param, "veryfast", "zerolatency");
        if (ret_val != 0) {
            WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                         "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
                         ret_val);
            x264_encoder_close(encoder_);
            encoder_ = NULL;
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
        /* Configure non-default params */
        /*cpuFlags 去空缓冲区继续使用不死锁保证*/
        param.i_threads = X264_SYNC_LOOKAHEAD_AUTO;
        param.i_width = inst->width;
        param.i_height = inst->height;
        param.i_frame_total   =0;//要编码的总帧数，不知道用0
        param.i_keyint_max    =50;
        param.i_bframe        =5;
        param.b_open_gop      =0;
        param.i_bframe_pyramid=0;
        param.i_bframe_adaptive=X264_B_ADAPT_TRELLIS;
    /*log参数，不需要打印编码信息时直接注释掉*/
//    param.i_log_level    =X264_LOG_DEBUG;
        param.i_fps_den               =1;//码率分母
        param.i_fps_num               =25;//码率分子
        param.b_intra_refresh =1;
        param.b_annexb = 1;//这里设置为1，是为了使编码后的NAL统一有4字节的起始码
        param.i_csp = X264_CSP_I420;
    
        param.b_vfr_input = 0;
        param.b_repeat_headers = 1; //sps, pps
        param.rc.i_bitrate = codec_settings_.maxBitrate;
        /* Apply profile restrictions. */
        ret_val = x264_param_apply_profile(&param, "baseline");
        if (ret_val != 0) {
            WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                         "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
                         ret_val);
            x264_encoder_close(encoder_);
            encoder_ = NULL;
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
    
//    初始化pic
        ret_val = x264_picture_alloc(&pic_, param.i_csp, param.i_width, param.i_height);
        if (ret_val != 0) {
            WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                         "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
                         ret_val);
            x264_encoder_close(encoder_);
            encoder_ = NULL;
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
    
//    pic_.img.plane[0] = encoder->yuv;
//    encoder->yuv420p_picture->img.plane[1] = encoder->yuv+IMAGE_WIDTH*IMAGE_HEIGHT;
//    encoder->yuv420p_picture->img.plane[2] = encoder->yuv+IMAGE_WIDTH*IMAGE_HEIGHT+IMAGE_WIDTH*IMAGE_HEIGHT/4;
    
//     打开编码器
    
        encoder_ = x264_encoder_open(&param);
        if (!encoder_){
            WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                         "H264EncoderImpl::InitEncode() fails to initialize encoder ret_val %d",
                         ret_val);
            x264_encoder_close(encoder_);
            x264_picture_clean(&pic_);
            encoder_ = NULL;
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
    
        if (encoded_image_._buffer != NULL) {
            delete[] encoded_image_._buffer;
        }
        encoded_image_._size = CalcBufferSize(kI420, codec_settings_.width, codec_settings_.height);
        encoded_image_._buffer = new uint8_t[encoded_image_._size];
        encoded_image_._completeFrame = true;
        
        inited_ = true;
        WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
                     "H264EncoderImpl::InitEncode(width:%d, height:%d, framerate:%d, start_bitrate:%d, max_bitrate:%d)",
                     inst->width, inst->height, inst->maxFramerate, inst->startBitrate, inst->maxBitrate);
        
        return WEBRTC_VIDEO_CODEC_OK;  
}


int32_t H264EncoderImpl::Release() {
  if (encoder_) {
    encoder_ = nullptr;
  }
  if (encoded_image_._buffer != nullptr) {
    encoded_image_._buffer = nullptr;
    encoded_image_buffer_.reset();
  }
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t H264EncoderImpl::RegisterEncodeCompleteCallback(
    EncodedImageCallback* callback) {
  encoded_image_callback_ = callback;
  return WEBRTC_VIDEO_CODEC_OK;
}

    int32_t H264EncoderImpl::SetRates(uint32_t bitrate, uint32_t framerate) {
        if (bitrate <= 0 || framerate <= 0) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        codec_settings_.targetBitrate = bitrate;
        codec_settings_.maxFramerate = framerate;
        
              return WEBRTC_VIDEO_CODEC_OK;
    }

int32_t H264EncoderImpl::Encode(
    const VideoFrame& input_image, const CodecSpecificInfo* codec_specific_info,
    const std::vector<VideoFrameType>* frame_types) {
        if (!inited_) {
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        if (input_image.IsZeroSize()) {
            return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
        }
        if (encoded_image_callback_ == NULL) {
            return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
        }
        
        VideoFrameType frame_type = kDeltaFrame;
        // We only support one stream at the moment.
        if (frame_types && frame_types->size() > 0) {
            frame_type = (*frame_types)[0];
        }
        
//        bool send_keyframe = (frame_type == kKeyFrame);
//        if (send_keyframe) {
//            pic_.b_keyframe = true;
//            WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
//                         "H264EncoderImpl::EncodeKeyFrame(width:%d, height:%d)",
//                         input_image.width(), input_image.height());
//        }
    
    
        /* Read input frame */
     pic_.img.i_csp =X264_CSP_I420;
        pic_.img.i_plane = 3;
        pic_.i_type = X264_TYPE_AUTO;
    pic_.img.plane[0] = const_cast<uint8_t*>(input_image.buffer(kYPlane));
        pic_.img.plane[1] = const_cast<uint8_t*>(input_image.buffer(kUPlane));
        pic_.img.plane[2] = const_cast<uint8_t*>(input_image.buffer(kVPlane));
//        pic_.i_pts = i_frame;
       pic_.i_pts++;
        int n_nal = 0;
        int i_frame_size = x264_encoder_encode(encoder_, &nal_t_, &n_nal, &pic_, &pic_out_);
        if (i_frame_size < 0)
        {
            WEBRTC_TRACE(webrtc::kTraceError, webrtc::kTraceVideoCoding, -1,
                         "H264EncoderImpl::Encode() fails to encode %d",
                         i_frame_size);
            x264_encoder_close(encoder_);
            x264_picture_clean(&pic_);
            encoder_ = NULL;
            return WEBRTC_VIDEO_CODEC_ERROR;
        }
//   if((ret = x264_encoder_encode(encoder->x264_encoder,&encoder->nal,&n_nal,encoder->yuv420p_picture,&pic_out))<0){ 
//    for(my_nal = encoder->nal; my_nal<encoder->nal+n_nal; ++my_nal){
//        write(fd_write,my_nal->p_payload,my_nal->i_payload);
//    }
        RTPFragmentationHeader frag_info;
    
        if (i_frame_size)
        {
            if (n_nal == 0) {
                return WEBRTC_VIDEO_CODEC_OK;
            }
            frag_info.VerifyAndAllocateFragmentationHeader(n_nal);
            encoded_image_._length = 0;
            
            uint32_t totalNaluIndex = 0;
            for (int nal_index = 0; nal_index < n_nal; nal_index++)
            {
                uint32_t currentNaluSize = 0;
                currentNaluSize = nal_t_[nal_index].i_payload; //x264_encoder_encode编码得到的nal单元是已经带有起始码的，此外，这里直接使用nal[index]即可，不必再使用x264_nal_encode函数
                memcpy(encoded_image_._buffer + encoded_image_._length, nal_t_[nal_index].p_payload, currentNaluSize);//encoded_image_中存有的是去掉起始码的数据
                encoded_image_._length += currentNaluSize;
                
                WEBRTC_TRACE(webrtc::kTraceApiCall, webrtc::kTraceVideoCoding, -1,
                             "H264EncoderImpl::Encode() nal_type %d, length:%d",
                             nal_t_[nal_index].i_type, encoded_image_._length);
                
                
                frag_info.fragmentationOffset[totalNaluIndex] = encoded_image_._length - currentNaluSize;
                frag_info.fragmentationLength[totalNaluIndex] = currentNaluSize;
                frag_info.fragmentationPlType[totalNaluIndex] = nal_t_[nal_index].i_type;
                frag_info.fragmentationTimeDiff[totalNaluIndex] = 0;
                totalNaluIndex++;
            }
        }
        i_frame++;
        if (encoded_image_._length > 0) {
            encoded_image_._timeStamp = input_image.timestamp();
            encoded_image_.capture_time_ms_ = input_image.render_time_ms();
            encoded_image_._encodedHeight = codec_settings_.height;
            encoded_image_._encodedWidth = codec_settings_.width;
            encoded_image_._frameType = frame_type;
            // call back
            encoded_image_callback_->Encoded(encoded_image_, NULL, &frag_info);
        }  
        return WEBRTC_VIDEO_CODEC_OK;
}

bool H264EncoderImpl::IsInitialized() const {
  return encoder_ != nullptr;
}

int32_t H264EncoderImpl::SetChannelParameters(
    uint32_t packet_loss, int64_t rtt) {
  return WEBRTC_VIDEO_CODEC_OK;
}

int32_t H264EncoderImpl::SetPeriodicKeyFrames(bool enable) {
  return WEBRTC_VIDEO_CODEC_OK;
}

void H264EncoderImpl::OnDroppedFrame() {
}

}  // namespace webrtc
