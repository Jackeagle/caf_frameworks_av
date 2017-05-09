/*
 * Copyright 2016 The Android Open Source Project
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

#define LOG_TAG "AudioStreamRecord"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <stdint.h>
#include <utils/String16.h>
#include <media/AudioRecord.h>
#include <aaudio/AAudio.h>

#include "AudioClock.h"
#include "legacy/AudioStreamLegacy.h"
#include "legacy/AudioStreamRecord.h"
#include "utility/FixedBlockWriter.h"

using namespace android;
using namespace aaudio;

AudioStreamRecord::AudioStreamRecord()
    : AudioStreamLegacy()
    , mFixedBlockWriter(*this)
{
}

AudioStreamRecord::~AudioStreamRecord()
{
    const aaudio_stream_state_t state = getState();
    bool bad = !(state == AAUDIO_STREAM_STATE_UNINITIALIZED || state == AAUDIO_STREAM_STATE_CLOSED);
    ALOGE_IF(bad, "stream not closed, in state %d", state);
}

aaudio_result_t AudioStreamRecord::open(const AudioStreamBuilder& builder)
{
    aaudio_result_t result = AAUDIO_OK;

    result = AudioStream::open(builder);
    if (result != AAUDIO_OK) {
        return result;
    }

    // Try to create an AudioRecord

    // TODO Support UNSPECIFIED in AudioTrack. For now, use stereo if unspecified.
    int32_t samplesPerFrame = (getSamplesPerFrame() == AAUDIO_UNSPECIFIED)
                              ? 2 : getSamplesPerFrame();
    audio_channel_mask_t channelMask = audio_channel_in_mask_from_count(samplesPerFrame);

    audio_input_flags_t flags = (audio_input_flags_t) AUDIO_INPUT_FLAG_NONE;

    size_t frameCount = (builder.getBufferCapacity() == AAUDIO_UNSPECIFIED) ? 0
                        : builder.getBufferCapacity();
    // TODO implement an unspecified Android format then use that.
    audio_format_t format = (getFormat() == AAUDIO_UNSPECIFIED)
            ? AUDIO_FORMAT_PCM_FLOAT
            : AAudioConvert_aaudioToAndroidDataFormat(getFormat());

    // Setup the callback if there is one.
    AudioRecord::callback_t callback = nullptr;
    void *callbackData = nullptr;
    AudioRecord::transfer_type streamTransferType = AudioRecord::transfer_type::TRANSFER_SYNC;
    if (builder.getDataCallbackProc() != nullptr) {
        streamTransferType = AudioRecord::transfer_type::TRANSFER_CALLBACK;
        callback = getLegacyCallback();
        callbackData = this;
    }
    mCallbackBufferSize = builder.getFramesPerDataCallback();

    mAudioRecord = new AudioRecord(
            AUDIO_SOURCE_DEFAULT,
            getSampleRate(),
            format,
            channelMask,
            mOpPackageName, // const String16& opPackageName TODO does not compile
            frameCount,
            callback,
            callbackData,
            0,    //    uint32_t notificationFrames = 0,
            AUDIO_SESSION_ALLOCATE,
            streamTransferType,
            flags
            //   int uid = -1,
            //   pid_t pid = -1,
            //   const audio_attributes_t* pAttributes = nullptr
            );

    // Did we get a valid track?
    status_t status = mAudioRecord->initCheck();
    if (status != OK) {
        close();
        ALOGE("AudioStreamRecord::open(), initCheck() returned %d", status);
        return AAudioConvert_androidToAAudioResult(status);
    }

    // Get the actual rate.
    setSampleRate(mAudioRecord->getSampleRate());
    setSamplesPerFrame(mAudioRecord->channelCount());
    setFormat(AAudioConvert_androidToAAudioDataFormat(mAudioRecord->format()));

    // We may need to pass the data through a block size adapter to guarantee constant size.
    if (mCallbackBufferSize != AAUDIO_UNSPECIFIED) {
        int callbackSizeBytes = getBytesPerFrame() * mCallbackBufferSize;
        mFixedBlockWriter.open(callbackSizeBytes);
        mBlockAdapter = &mFixedBlockWriter;
    } else {
        mBlockAdapter = nullptr;
    }

    setState(AAUDIO_STREAM_STATE_OPEN);

    return AAUDIO_OK;
}

aaudio_result_t AudioStreamRecord::close()
{
    // TODO add close() or release() to AudioRecord API then call it from here
    if (getState() != AAUDIO_STREAM_STATE_CLOSED) {
        mAudioRecord.clear();
        setState(AAUDIO_STREAM_STATE_CLOSED);
    }
    mFixedBlockWriter.close();
    return AAUDIO_OK;
}

void AudioStreamRecord::processCallback(int event, void *info) {

    ALOGD("AudioStreamRecord::processCallback(), event %d", event);
    switch (event) {
        case AudioRecord::EVENT_MORE_DATA:
            processCallbackCommon(AAUDIO_CALLBACK_OPERATION_PROCESS_DATA, info);
            break;

            // Stream got rerouted so we disconnect.
        case AudioRecord::EVENT_NEW_IAUDIORECORD:
            processCallbackCommon(AAUDIO_CALLBACK_OPERATION_DISCONNECTED, info);
            break;

        default:
            break;
    }
    return;
}

aaudio_result_t AudioStreamRecord::requestStart()
{
    if (mAudioRecord.get() == nullptr) {
        return AAUDIO_ERROR_INVALID_STATE;
    }
    // Get current position so we can detect when the track is playing.
    status_t err = mAudioRecord->getPosition(&mPositionWhenStarting);
    if (err != OK) {
        return AAudioConvert_androidToAAudioResult(err);
    }

    err = mAudioRecord->start();
    if (err != OK) {
        return AAudioConvert_androidToAAudioResult(err);
    } else {
        setState(AAUDIO_STREAM_STATE_STARTING);
    }
    return AAUDIO_OK;
}

aaudio_result_t AudioStreamRecord::requestPause()
{
    return AAUDIO_ERROR_UNIMPLEMENTED;
}

aaudio_result_t AudioStreamRecord::requestFlush() {
    return AAUDIO_ERROR_UNIMPLEMENTED;
}

aaudio_result_t AudioStreamRecord::requestStop() {
    if (mAudioRecord.get() == nullptr) {
        return AAUDIO_ERROR_INVALID_STATE;
    }
    setState(AAUDIO_STREAM_STATE_STOPPING);
    mAudioRecord->stop();
    return AAUDIO_OK;
}

aaudio_result_t AudioStreamRecord::updateStateWhileWaiting()
{
    aaudio_result_t result = AAUDIO_OK;
    aaudio_wrapping_frames_t position;
    status_t err;
    switch (getState()) {
    // TODO add better state visibility to AudioRecord
    case AAUDIO_STREAM_STATE_STARTING:
        err = mAudioRecord->getPosition(&position);
        if (err != OK) {
            result = AAudioConvert_androidToAAudioResult(err);
        } else if (position != mPositionWhenStarting) {
            setState(AAUDIO_STREAM_STATE_STARTED);
        }
        break;
    case AAUDIO_STREAM_STATE_STOPPING:
        if (mAudioRecord->stopped()) {
            setState(AAUDIO_STREAM_STATE_STOPPED);
        }
        break;
    default:
        break;
    }
    return result;
}

aaudio_result_t AudioStreamRecord::read(void *buffer,
                                      int32_t numFrames,
                                      int64_t timeoutNanoseconds)
{
    int32_t bytesPerFrame = getBytesPerFrame();
    int32_t numBytes;
    aaudio_result_t result = AAudioConvert_framesToBytes(numFrames, bytesPerFrame, &numBytes);
    if (result != AAUDIO_OK) {
        return result;
    }

    // TODO add timeout to AudioRecord
    bool blocking = (timeoutNanoseconds > 0);
    ssize_t bytesRead = mAudioRecord->read(buffer, numBytes, blocking);
    if (bytesRead == WOULD_BLOCK) {
        return 0;
    } else if (bytesRead < 0) {
        return AAudioConvert_androidToAAudioResult(bytesRead);
    }
    int32_t framesRead = (int32_t)(bytesRead / bytesPerFrame);
    return (aaudio_result_t) framesRead;
}

aaudio_result_t AudioStreamRecord::setBufferSize(int32_t requestedFrames)
{
    return getBufferSize();
}

int32_t AudioStreamRecord::getBufferSize() const
{
    return getBufferCapacity(); // TODO implement in AudioRecord?
}

int32_t AudioStreamRecord::getBufferCapacity() const
{
    return static_cast<int32_t>(mAudioRecord->frameCount());
}

int32_t AudioStreamRecord::getXRunCount() const
{
    return AAUDIO_ERROR_UNIMPLEMENTED; // TODO implement when AudioRecord supports it
}

int32_t AudioStreamRecord::getFramesPerBurst() const
{
    return static_cast<int32_t>(mAudioRecord->getNotificationPeriodInFrames());
}

aaudio_result_t AudioStreamRecord::getTimestamp(clockid_t clockId,
                                               int64_t *framePosition,
                                               int64_t *timeNanoseconds) {
    ExtendedTimestamp extendedTimestamp;
    status_t status = mAudioRecord->getTimestamp(&extendedTimestamp);
    if (status != NO_ERROR) {
        return AAudioConvert_androidToAAudioResult(status);
    }
    // TODO Merge common code into AudioStreamLegacy after rebasing.
    int timebase;
    switch(clockId) {
        case CLOCK_BOOTTIME:
            timebase = ExtendedTimestamp::TIMEBASE_BOOTTIME;
            break;
        case CLOCK_MONOTONIC:
            timebase = ExtendedTimestamp::TIMEBASE_MONOTONIC;
            break;
        default:
            ALOGE("getTimestamp() - Unrecognized clock type %d", (int) clockId);
            return AAUDIO_ERROR_UNEXPECTED_VALUE;
            break;
    }
    status = extendedTimestamp.getBestTimestamp(framePosition, timeNanoseconds, timebase);
    return AAudioConvert_androidToAAudioResult(status);
}
