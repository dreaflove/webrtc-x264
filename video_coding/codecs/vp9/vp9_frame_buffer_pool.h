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

#ifndef WEBRTC_MODULES_VIDEO_CODING_CODECS_VP9_FRAME_BUFFER_POOL_H_
#define WEBRTC_MODULES_VIDEO_CODING_CODECS_VP9_FRAME_BUFFER_POOL_H_

#include <vector>

#include "base/basictypes.h"
#include "base/buffer.h"
#include "base/criticalsection.h"
#include "base/refcount.h"
#include "base/scoped_ref_ptr.h"

struct vpx_codec_ctx;
struct vpx_codec_frame_buffer;

namespace webrtc {

// This memory pool is used to serve buffers to libvpx for decoding purposes in
// VP9, which is set up in InitializeVPXUsePool. After the initialization any
// time libvpx wants to decode a frame it will use buffers provided and released
// through VpxGetFrameBuffer and VpxReleaseFrameBuffer.
// The benefit of owning the pool that libvpx relies on for decoding is that the
// decoded frames returned by libvpx (from vpx_codec_get_frame) use parts of our
// buffers for the decoded image data. By retaining ownership of this buffer
// using scoped_refptr, the image buffer can be reused by VideoFrames and no
// frame copy has to occur during decoding and frame delivery.
//
// Pseudo example usage case:
//    Vp9FrameBufferPool pool;
//    pool.InitializeVpxUsePool(decoder_ctx);
//    ...
//
//    // During decoding, libvpx will get and release buffers from the pool.
//    vpx_codec_decode(decoder_ctx, ...);
//
//    vpx_image_t* img = vpx_codec_get_frame(decoder_ctx, &iter);
//    // Important to use scoped_refptr to protect it against being recycled by
//    // the pool.
//    scoped_refptr<Vp9FrameBuffer> img_buffer = (Vp9FrameBuffer*)img->fb_priv;
//    ...
//
//    // Destroying the codec will make libvpx release any buffers it was using.
//    vpx_codec_destroy(decoder_ctx);
class Vp9FrameBufferPool {
 public:
  class Vp9FrameBuffer : public rtc::RefCountInterface {
   public:
    uint8_t* GetData();
    size_t GetDataSize() const;
    void SetSize(size_t size);

    virtual bool HasOneRef() const = 0;

   private:
    // Data as an easily resizable buffer.
    rtc::Buffer data_;
  };

  // Configures libvpx to, in the specified context, use this memory pool for
  // buffers used to decompress frames. This is only supported for VP9.
  bool InitializeVpxUsePool(vpx_codec_ctx* vpx_codec_context);

  // Gets a frame buffer of at least |min_size|, recycling an available one or
  // creating a new one. When no longer referenced from the outside the buffer
  // becomes recyclable.
  rtc::scoped_refptr<Vp9FrameBuffer> GetFrameBuffer(size_t min_size);
  // Gets the number of buffers currently in use (not ready to be recycled).
  int GetNumBuffersInUse() const;
  // Releases allocated buffers, deleting available buffers. Buffers in use are
  // not deleted until they are no longer referenced.
  void ClearPool();

  // InitializeVpxUsePool configures libvpx to call this function when it needs
  // a new frame buffer. Parameters:
  // |user_priv| Private data passed to libvpx, InitializeVpxUsePool sets it up
  //             to be a pointer to the pool.
  // |min_size|  Minimum size needed by libvpx (to decompress a frame).
  // |fb|        Pointer to the libvpx frame buffer object, this is updated to
  //             use the pool's buffer.
  // Returns 0 on success. Returns < 0 on failure.
  static int32 VpxGetFrameBuffer(void* user_priv,
                                 size_t min_size,
                                 vpx_codec_frame_buffer* fb);

  // InitializeVpxUsePool configures libvpx to call this function when it has
  // finished using one of the pool's frame buffer. Parameters:
  // |user_priv| Private data passed to libvpx, InitializeVpxUsePool sets it up
  //             to be a pointer to the pool.
  // |fb|        Pointer to the libvpx frame buffer object, its |priv| will be
  //             a pointer to one of the pool's Vp9FrameBuffer.
  static int32 VpxReleaseFrameBuffer(void* user_priv,
                                     vpx_codec_frame_buffer* fb);

 private:
  // Protects |allocated_buffers_|.
  mutable rtc::CriticalSection buffers_lock_;
  // All buffers, in use or ready to be recycled.
  std::vector<rtc::scoped_refptr<Vp9FrameBuffer>> allocated_buffers_
      GUARDED_BY(buffers_lock_);
  // If more buffers than this are allocated we print warnings, and crash if
  // in debug mode.
  static const size_t max_num_buffers_ = 10;
};

}  // namespace webrtc

#endif  // WEBRTC_MODULES_VIDEO_CODING_CODECS_VP9_FRAME_BUFFER_POOL_H_
