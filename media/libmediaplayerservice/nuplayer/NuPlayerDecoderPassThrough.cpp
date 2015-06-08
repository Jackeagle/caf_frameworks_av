/*
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
#define LOG_TAG "NuPlayerDecoderPassThrough"
#include <utils/Log.h>
#include <inttypes.h>

#include "NuPlayerDecoderPassThrough.h"

#include "NuPlayerRenderer.h"
#include "NuPlayerSource.h"

#include <media/ICrypto.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaDefs.h>

#include "ATSParser.h"

#include "ExtendedUtils.h"

namespace android {

// Reduced the aggregate buffer size to 66K to make it
// equivalent to compress offload buffer size for resolving
// Gapless and AV sync issues.
const size_t NuPlayer::kAggregateBufferSizeBytes = 66 * 1024;
static const size_t kMaxCachedBytes = 200000;

NuPlayer::DecoderPassThrough::DecoderPassThrough(
        const sp<AMessage> &notify,
        const sp<Source> &source,
        const sp<Renderer> &renderer)
    : DecoderBase(notify),
      mSource(source),
      mRenderer(renderer),
      mSkipRenderingUntilMediaTimeUs(-1ll),
      mPaused(false),
      mReachedEOS(true),
      mPendingAudioErr(OK),
      mIsVorbis(false),
      mInitBuffer(true),
      mPendingBuffersToDrain(0),
      mCachedBytes(0),
      mComponentName("pass through decoder"),
      mPCMFormat(AUDIO_FORMAT_INVALID) {
    ALOGW_IF(renderer == NULL, "expect a non-NULL renderer");
}

NuPlayer::DecoderPassThrough::~DecoderPassThrough() {
}

void NuPlayer::DecoderPassThrough::getStats(
        int64_t *numFramesTotal, int64_t *numFramesDropped) const {
    *numFramesTotal = 0;
    *numFramesDropped = 0;
}

void NuPlayer::DecoderPassThrough::onConfigure(const sp<AMessage> &format, bool isStreaming) {
    ALOGV("[%s] onConfigure", mComponentName.c_str());
    sp<AMessage> videoFormat = mSource->getFormat(false /* video */);
    bool hasVideo = (videoFormat != NULL);
    mCachedBytes = 0;
    mPendingBuffersToDrain = 0;
    mReachedEOS = false;
    ++mBufferGeneration;

    sp<MetaData> audioMeta = mSource->getFormatMeta(true /* audio */);

    // The audio sink is already opened before the PassThrough decoder is created.
    // Opening again might be relevant if decoder is instantiated after shutdown and
    // format is different.
    if (ExtendedUtils::is24bitPCMOffloadEnabled()) {
        if (ExtendedUtils::is24bitPCMOffloaded(audioMeta)) {
            format->setInt32("sbit", 24);
        }
    }

    if(ExtendedUtils::isWMAFormat(audioMeta)) {
        int32_t wmaVersion;
        if (ExtendedUtils::getWMAVersion(audioMeta, &wmaVersion) == OK) {
            format->setInt32("wmav", wmaVersion);
        }
    }

    bool isAlacFormat = ExtendedUtils::isALACFormat(audioMeta);
    bool isApeFormat = ExtendedUtils::isAPEFormat(audioMeta);

    if (isAlacFormat || isApeFormat) {
        ALOGV("Detected clip of %s format", isAlacFormat ? "alac" : "ape");
        if (ExtendedUtils::getPcmSampleBits(audioMeta) == 24) {
            ALOGV("Set bit width for 24 bit %s clip", isAlacFormat ? "alac" : "ape");
            format->setInt32("sbit", 24);
        }
    }

    mIsVorbis = ExtendedUtils::isVorbisFormat(audioMeta);

    const char * mime;
    audioMeta->findCString(kKeyMIMEType, &mime);
    bool pcm = mime && !strcasecmp(MEDIA_MIMETYPE_AUDIO_RAW, mime);
    if (pcm) {
        audio_format_t pcmFormat = (audio_format_t)ExtendedUtils::getPCMFormat(audioMeta);
        if (pcmFormat != AUDIO_FORMAT_INVALID) {
            mPCMFormat = pcmFormat;
            format->setInt32("pcm-format", (int32_t)pcmFormat);
        }
    }

    status_t err = mRenderer->openAudioSink(
            format, true /* offloadOnly */, hasVideo /* hasVideo */, isStreaming,
            AUDIO_OUTPUT_FLAG_NONE /* flags */, NULL /* isOffloaded */);
    if (err != OK) {
        handleError(err);
    } else {
        onRequestInputBuffers();
    }
}

void NuPlayer::DecoderPassThrough::onSetRenderer(
        const sp<Renderer> &renderer) {
    // renderer can't be changed during offloading
    ALOGW_IF(renderer != mRenderer,
            "ignoring request to change renderer");
}

void NuPlayer::DecoderPassThrough::onGetInputBuffers(
        Vector<sp<ABuffer> > * /* dstBuffers */) {
    ALOGE("onGetInputBuffers() called unexpectedly");
}

bool NuPlayer::DecoderPassThrough::isStaleReply(const sp<AMessage> &msg) {
    int32_t generation;
    CHECK(msg->findInt32("generation", &generation));
    return generation != mBufferGeneration;
}

bool NuPlayer::DecoderPassThrough::isDoneFetching() const {
    ALOGV("[%s] mCachedBytes = %zu, mReachedEOS = %d mPaused = %d",
            mComponentName.c_str(), mCachedBytes, mReachedEOS, mPaused);

    return mCachedBytes >= kMaxCachedBytes || mReachedEOS || mPaused;
}

void NuPlayer::DecoderPassThrough::doRequestBuffers() {
    status_t err = OK;
    while (!isDoneFetching()) {
        sp<AMessage> msg = new AMessage();

        err = fetchInputData(msg);
        if (err != OK) {
            break;
        }

        onInputBufferFetched(msg);
    }

    if (err == -EWOULDBLOCK
            && mSource->feedMoreTSData() == OK) {
        scheduleRequestBuffers();
    }
}

status_t NuPlayer::DecoderPassThrough::dequeueAccessUnit(sp<ABuffer> *accessUnit) {
    status_t err;

    // Did we save an accessUnit earlier because of a discontinuity?
    if (mPendingAudioAccessUnit != NULL) {
        *accessUnit = mPendingAudioAccessUnit;
        mPendingAudioAccessUnit.clear();
        err = mPendingAudioErr;
        ALOGV("feedDecoderInputData() use mPendingAudioAccessUnit");
    } else {
        err = mSource->dequeueAccessUnit(true /* audio */, accessUnit);
    }

    if (err == INFO_DISCONTINUITY || err == ERROR_END_OF_STREAM) {
        if (mAggregateBuffer != NULL) {
            // We already have some data so save this for later.
            mPendingAudioErr = err;
            mPendingAudioAccessUnit = *accessUnit;
            (*accessUnit).clear();
            ALOGD("return aggregated buffer and save err(=%d) for later", err);
            err = OK;
        }
    }

    return err;
}

sp<ABuffer> NuPlayer::DecoderPassThrough::aggregateBuffer(
        const sp<ABuffer> &accessUnit) {
    sp<ABuffer> aggregate;

    if (accessUnit == NULL) {
        // accessUnit is saved to mPendingAudioAccessUnit
        // return current mAggregateBuffer
        aggregate = mAggregateBuffer;
        mAggregateBuffer.clear();
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
    if (mIsVorbis && mInitBuffer) {
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
        mInitBuffer = false;
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
        } else {
            // Grab time from first small buffer if available.
            if ((bigSize == 0) && smallTimestampValid) {
                mAggregateBuffer->meta()->setInt64("timeUs", timeUs);
            }
            // Append small buffer to the bigger buffer.
            if (mIsVorbis) {
                if (mAnchorBuffer != NULL) {
                    // prepend vorbis header whenever available
                    CHECK(bigSize == 0);
                    memcpy(mAggregateBuffer->base(), mAnchorBuffer->data(), mAnchorBuffer->size());
                    mAggregateBuffer->setRange(0, mAnchorBuffer->size());
                    bigSize += mVorbisHdrBuffer->size();
                    mVorbisHdrBuffer.clear();
                    mAnchorBuffer.clear();
                } else {
                    // Convert frame stream to adapt to dsp vorbis decoder.
                    // Trim 4bytes appended, and prepend 4bytes required (assume one frame per buffer).
                    // NOTE: extra data is used for decode error recovery at the less frame drop possible.
                    int32_t frameSize = smallSize - sizeof(int32_t);
                    memcpy(mAggregateBuffer->base() + bigSize, &frameSize, sizeof(int32_t));
                    memcpy(mAggregateBuffer->base() + bigSize + sizeof(int32_t),
                            accessUnit->data(), frameSize);
                }
            } else {
                memcpy(mAggregateBuffer->base() + bigSize, accessUnit->data(), smallSize);
            }
            bigSize += smallSize;
            mAggregateBuffer->setRange(0, bigSize);
            if (mPCMFormat != AUDIO_FORMAT_INVALID) {
                mAggregateBuffer->meta()->setInt32("pcm-format", (int32_t)mPCMFormat);
            }

            ALOGV("feedDecoderInputData() smallSize = %zu, bigSize = %zu, capacity = %zu",
                    smallSize, bigSize, mAggregateBuffer->capacity());
        }
    } else {
        // decided not to aggregate
        aggregate = accessUnit;
        if (mIsVorbis) {
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
    }

    return aggregate;
}

status_t NuPlayer::DecoderPassThrough::fetchInputData(sp<AMessage> &reply) {
    sp<ABuffer> accessUnit;

    do {
        status_t err = dequeueAccessUnit(&accessUnit);

        if (err == -EWOULDBLOCK) {
            return err;
        } else if (err != OK) {
            if (err == INFO_DISCONTINUITY) {
                int32_t type;
                CHECK(accessUnit->meta()->findInt32("discontinuity", &type));

                bool formatChange =
                        (type & ATSParser::DISCONTINUITY_AUDIO_FORMAT) != 0;

                bool timeChange =
                        (type & ATSParser::DISCONTINUITY_TIME) != 0;

                ALOGI("audio discontinuity (formatChange=%d, time=%d)",
                        formatChange, timeChange);

                if (formatChange || timeChange) {
                    sp<AMessage> msg = mNotify->dup();
                    msg->setInt32("what", kWhatInputDiscontinuity);
                    // will perform seamless format change,
                    // only notify NuPlayer to scan sources
                    msg->setInt32("formatChange", false);
                    msg->post();
                }

                if (timeChange) {
                    onFlush(false /* notifyComplete */);
                    err = OK;
                } else if (formatChange) {
                    // do seamless format change
                    err = OK;
                } else {
                    // This stream is unaffected by the discontinuity
                    return -EWOULDBLOCK;
                }
            }

            reply->setInt32("err", err);
            return OK;
        }

        accessUnit = aggregateBuffer(accessUnit);
    } while (accessUnit == NULL);

#if 0
    int64_t mediaTimeUs;
    CHECK(accessUnit->meta()->findInt64("timeUs", &mediaTimeUs));
    ALOGV("feeding audio input buffer at media time %.2f secs",
         mediaTimeUs / 1E6);
#endif

    reply->setBuffer("buffer", accessUnit);

    return OK;
}

void NuPlayer::DecoderPassThrough::onInputBufferFetched(
        const sp<AMessage> &msg) {
    if (mReachedEOS) {
        return;
    }

    sp<ABuffer> buffer;
    bool hasBuffer = msg->findBuffer("buffer", &buffer);
    if (buffer == NULL) {
        int32_t streamErr = ERROR_END_OF_STREAM;
        CHECK(msg->findInt32("err", &streamErr) || !hasBuffer);
        if (streamErr == OK) {
            return;
        }

        mReachedEOS = true;
        if (mRenderer != NULL) {
            mRenderer->queueEOS(true /* audio */, ERROR_END_OF_STREAM);
        }
        return;
    }

    sp<AMessage> extra;
    if (buffer->meta()->findMessage("extra", &extra) && extra != NULL) {
        int64_t resumeAtMediaTimeUs;
        if (extra->findInt64(
                    "resume-at-mediatimeUs", &resumeAtMediaTimeUs)) {
            ALOGI("[%s] suppressing rendering until %lld us",
                    mComponentName.c_str(), (long long)resumeAtMediaTimeUs);
            mSkipRenderingUntilMediaTimeUs = resumeAtMediaTimeUs;
        }
    }

    int32_t bufferSize = buffer->size();
    mCachedBytes += bufferSize;

    if (mSkipRenderingUntilMediaTimeUs >= 0) {
        int64_t timeUs = 0;
        CHECK(buffer->meta()->findInt64("timeUs", &timeUs));

        if (timeUs < mSkipRenderingUntilMediaTimeUs) {
            ALOGV("[%s] dropping buffer at time %lld as requested.",
                     mComponentName.c_str(), (long long)timeUs);

            onBufferConsumed(bufferSize);
            return;
        }

        mSkipRenderingUntilMediaTimeUs = -1;
    }

    if (mRenderer == NULL) {
        onBufferConsumed(bufferSize);
        return;
    }

    sp<AMessage> reply = new AMessage(kWhatBufferConsumed, id());
    reply->setInt32("generation", mBufferGeneration);
    reply->setInt32("size", bufferSize);

    mRenderer->queueBuffer(true /* audio */, buffer, reply);

    ++mPendingBuffersToDrain;
    ALOGV("onInputBufferFilled: #ToDrain = %zu, cachedBytes = %zu",
            mPendingBuffersToDrain, mCachedBytes);
}

void NuPlayer::DecoderPassThrough::onBufferConsumed(int32_t size) {
    --mPendingBuffersToDrain;
    mCachedBytes -= size;
    ALOGV("onBufferConsumed: #ToDrain = %zu, cachedBytes = %zu",
            mPendingBuffersToDrain, mCachedBytes);
    onRequestInputBuffers();
}

void NuPlayer::DecoderPassThrough::onResume(bool notifyComplete) {
    mPaused = false;

    onRequestInputBuffers();

    if (notifyComplete) {
        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatResumeCompleted);
        notify->post();
    }
}

void NuPlayer::DecoderPassThrough::onFlush(bool notifyComplete) {
    ++mBufferGeneration;
    mSkipRenderingUntilMediaTimeUs = -1;
    mPendingAudioAccessUnit.clear();
    mPendingAudioErr = OK;
    mAggregateBuffer.clear();

    if (mRenderer != NULL) {
        mRenderer->flush(true /* audio */, notifyComplete);
        mRenderer->signalTimeDiscontinuity();
    }

    if (notifyComplete) {
        mPaused = true;
        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatFlushCompleted);
        notify->post();
    }

    mPendingBuffersToDrain = 0;
    mCachedBytes = 0;
    mReachedEOS = false;
}

void NuPlayer::DecoderPassThrough::onShutdown(bool notifyComplete) {
    ++mBufferGeneration;
    mSkipRenderingUntilMediaTimeUs = -1;

    if (notifyComplete) {
        sp<AMessage> notify = mNotify->dup();
        notify->setInt32("what", kWhatShutdownCompleted);
        notify->post();
    }

    mReachedEOS = true;
}

void NuPlayer::DecoderPassThrough::onMessageReceived(const sp<AMessage> &msg) {
    ALOGV("[%s] onMessage: %s", mComponentName.c_str(),
            msg->debugString().c_str());

    switch (msg->what()) {
        case kWhatBufferConsumed:
        {
            if (!isStaleReply(msg)) {
                int32_t size;
                CHECK(msg->findInt32("size", &size));
                onBufferConsumed(size);
            }
            break;
        }

        default:
            DecoderBase::onMessageReceived(msg);
            break;
    }
}

}  // namespace android
