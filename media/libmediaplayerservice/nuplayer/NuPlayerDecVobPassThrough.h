/*
 * Copyright (C) 2015, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef NUPLAYER_VORBIS_DECODER_PASS_THROUGH_H_

#define NUPLAYER_VORBIS_DECODER_PASS_THROUGH_H_

#include "NuPlayer.h"

#include "NuPlayerDecoderPassThrough.h"

namespace android {

struct NuPlayer::VorbisDecoderPassThrough : public DecoderPassThrough {
    VorbisDecoderPassThrough(const sp<AMessage> &notify,
                       const sp<Source> &source,
                       const sp<Renderer> &renderer);

protected:

    virtual ~VorbisDecoderPassThrough();

    virtual void onResume(bool notifyComplete);
    virtual void onFlush(bool notifyComplete);

private:

    bool mVorbisHdrRequired;
    bool mVorbisHdrCommitted;
    sp<ABuffer> mVorbisHdrBuffer;
    sp<ABuffer> mAnchorBuffer;

    sp<ABuffer> aggregateBuffer(const sp<ABuffer> &accessUnit);

    DISALLOW_EVIL_CONSTRUCTORS(VorbisDecoderPassThrough);
};

}  // namespace android

#endif  // NUPLAYER_VORBIS_DECODER_PASS_THROUGH_H_
