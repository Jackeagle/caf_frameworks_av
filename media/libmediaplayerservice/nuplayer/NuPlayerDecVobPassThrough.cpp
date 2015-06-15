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

//#define LOG_NDEBUG 0
#define LOG_TAG "NuPlayerDecVobPassThrough"
#include <utils/Log.h>

#include "NuPlayerDecVobPassThrough.h"

#include "NuPlayerRenderer.h"
#include "NuPlayerSource.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>

#include "ExtendedUtils.h"

namespace android {

NuPlayer::VorbisDecoderPassThrough::VorbisDecoderPassThrough(
        const sp<AMessage> &notify,
        const sp<Source> &source,
        const sp<Renderer> &renderer)
    : DecoderPassThrough(notify, source, renderer),
      mVorbisHdrRequired(true),
      mVorbisHdrCommitted(true) {
    ALOGW_IF(renderer == NULL, "expect a non-NULL renderer");
}

NuPlayer::VorbisDecoderPassThrough::~VorbisDecoderPassThrough() {
}

sp<ABuffer> NuPlayer::VorbisDecoderPassThrough::aggregateBuffer(
        const sp<ABuffer> &accessUnit) {
    sp<ABuffer> aggregate;

    if (accessUnit == NULL) {
        // accessUnit is saved to mPendingAudioAccessUnit
        // return current mAggregateBuffer
        aggregate = mAggregateBuffer;
        mAggregateBuffer.clear();
        mVorbisHdrCommitted = true;
        return aggregate;
    }

    size_t smallSize = accessUnit->size();
    if ((mAggregateBuffer == NULL)
            // Don't bother if only room for a few small buffers.
            && (smallSize < (kAggregateBufferSizeBytes / 3))) {
        // Create a larger buffer for combining smaller buffers from the extractor.
        mAggregateBuffer = new ABuffer(kAggregateBufferSizeBytes);
        mAggregateBuffer->setRange(0, 0); // start empty
    }

    // assemble vorbis header for the anchor buffer
    if (mVorbisHdrRequired) {
        sp<MetaData> audioMeta = mSource->getFormatMeta(true /* audio */);
        mVorbisHdrBuffer = ExtendedUtils::assembleVorbisHdr(audioMeta);
        CHECK(mVorbisHdrBuffer != NULL);

        size_t tmpSize = mVorbisHdrBuffer->size() + smallSize;
        mAnchorBuffer = new ABuffer(tmpSize);

        int32_t frameSize = smallSize - sizeof(int32_t);
        memcpy(mAnchorBuffer->base(), mVorbisHdrBuffer->data(), mVorbisHdrBuffer->size());
        memcpy(mAnchorBuffer->base() + mVorbisHdrBuffer->size(), &frameSize, sizeof(int32_t));
        memcpy(mAnchorBuffer->base() + mVorbisHdrBuffer->size() + sizeof(int32_t),
                accessUnit->data(), frameSize);

        mAnchorBuffer->setRange(0, tmpSize);
        mVorbisHdrRequired = false;
        // Don't clear mVorbisHdrBuffer right away, because it's still of use.
        // Lazy destroy instead.
    }


    if (mAggregateBuffer != NULL) {
        int64_t timeUs;
        int64_t dummy;
        bool smallTimestampValid = accessUnit->meta()->findInt64("timeUs", &timeUs);
        bool bigTimestampValid = mAggregateBuffer->meta()->findInt64("timeUs", &dummy);
        // Will the smaller buffer fit?
        size_t bigSize = mAggregateBuffer->size();
        size_t roomLeft = mAggregateBuffer->capacity() - bigSize;

        // Return anchor buffer directly if room left is not sufficient.
        if ((mAnchorBuffer != NULL) &&
            (roomLeft <= mAnchorBuffer->size())) {
            ALOGI("aggregate buffer room can't hold access unit with vorbis header");

            // mAnchorBuffer only appears as a first small buffer,
            // but the first small buffer isn't necessarily an anchor buffer.
            CHECK(bigSize == 0);
            if (smallTimestampValid) {
                mAnchorBuffer->meta()->setInt64("timeUs", timeUs);
            }

            mPendingAudioErr = OK;
            mPendingAudioAccessUnit = accessUnit;

            mAggregateBuffer.clear();
            mVorbisHdrBuffer.clear();
            return mAnchorBuffer;
        }

        // Should we save this small buffer for the next big buffer?
        // If the first small buffer did not have a timestamp then save
        // any buffer that does have a timestamp until the next big buffer.
        if ((smallSize > roomLeft)
            || (!bigTimestampValid && (bigSize > 0) && smallTimestampValid)) {
            mPendingAudioErr = OK;
            mPendingAudioAccessUnit = accessUnit;
            aggregate = mAggregateBuffer;
            mAggregateBuffer.clear();
            mVorbisHdrCommitted = true;
        } else {
            // Grab time from first small buffer if available.
            if ((bigSize == 0) && smallTimestampValid) {
                mAggregateBuffer->meta()->setInt64("timeUs", timeUs);
            }
            // Append small buffer to the bigger buffer.
            if (mAnchorBuffer != NULL) {
                // prepend vorbis header whenever available
                CHECK(bigSize == 0);
                memcpy(mAggregateBuffer->base(), mAnchorBuffer->data(), mAnchorBuffer->size());
                mAggregateBuffer->setRange(0, mAnchorBuffer->size());
                bigSize += mVorbisHdrBuffer->size();
                mVorbisHdrBuffer.clear();
                mAnchorBuffer.clear();
                mVorbisHdrCommitted = false;
            } else {
                // Convert frame stream to adapt to dsp vorbis decoder.
                // Trim 4bytes appended, and prepend 4bytes required (assume one frame per buffer).
                // NOTE: extra data is used for decode error recovery at the less frame drop possible.
                int32_t frameSize = smallSize - sizeof(int32_t);
                memcpy(mAggregateBuffer->base() + bigSize, &frameSize, sizeof(int32_t));
                memcpy(mAggregateBuffer->base() + bigSize + sizeof(int32_t),
                        accessUnit->data(), frameSize);
            }
            bigSize += smallSize;
            mAggregateBuffer->setRange(0, bigSize);

            ALOGV("feedDecoderInputData() smallSize = %zu, bigSize = %zu, capacity = %zu",
                    smallSize, bigSize, mAggregateBuffer->capacity());
        }
    } else {
        // decided not to aggregate
        if (mAnchorBuffer != NULL) {
            aggregate = mAnchorBuffer;
            mVorbisHdrBuffer.clear();
        } else {
            sp<ABuffer> transBuffer = new ABuffer(smallSize);
            int32_t frameSize = smallSize - sizeof(int32_t);
            memcpy(transBuffer->base(), &frameSize, sizeof(int32_t));
            memcpy(transBuffer->base() + sizeof(int32_t),
                    accessUnit->data(), frameSize);

            transBuffer->setRange(0, smallSize);
            aggregate = transBuffer;
        }

        int64_t timeUs;
        if (accessUnit->meta()->findInt64("timeUs", &timeUs)) {
            aggregate->meta()->setInt64("timeUs", timeUs);
        }
    }

    return aggregate;
}

void NuPlayer::VorbisDecoderPassThrough::onResume(bool notifyComplete) {
    // always resend vorbis header upon resume
    mVorbisHdrRequired = true;

    DecoderPassThrough::onResume(notifyComplete);
}

void NuPlayer::VorbisDecoderPassThrough::onFlush(bool notifyComplete) {
    // aggregation buffer will be discarded without being rendered
    // if vorbis header isn't yet committed, need to resend it again in next round
    if (!mVorbisHdrCommitted) {
        mVorbisHdrRequired = true;
    }

    DecoderPassThrough::onFlush(notifyComplete);
}

}  // namespace android
