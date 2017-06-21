/*
 * Copyright 2015 The Android Open Source Project
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

#define LOG_TAG "AAudio"
//#define LOG_NDEBUG 0
#include <utils/Log.h>

#include <atomic>
#include <stdint.h>
#include <aaudio/AAudio.h>

#include "AudioStreamBuilder.h"
#include "AudioStream.h"
#include "AudioClock.h"

using namespace aaudio;

AudioStream::AudioStream()
        : mCallbackEnabled(false)
{
    // mThread is a pthread_t of unknown size so we need memset.
    memset(&mThread, 0, sizeof(mThread));
    setPeriodNanoseconds(0);
}

aaudio_result_t AudioStream::open(const AudioStreamBuilder& builder)
{
    // Copy parameters from the Builder because the Builder may be deleted after this call.
    mSamplesPerFrame = builder.getSamplesPerFrame();
    mSampleRate = builder.getSampleRate();
    mDeviceId = builder.getDeviceId();
    mFormat = builder.getFormat();
    mSharingMode = builder.getSharingMode();
    mSharingModeMatchRequired = builder.isSharingModeMatchRequired();

    mPerformanceMode = builder.getPerformanceMode();

    // callbacks
    mFramesPerDataCallback = builder.getFramesPerDataCallback();
    mDataCallbackProc = builder.getDataCallbackProc();
    mErrorCallbackProc = builder.getErrorCallbackProc();
    mDataCallbackUserData = builder.getDataCallbackUserData();
    mErrorCallbackUserData = builder.getErrorCallbackUserData();

    // This is very helpful for debugging in the future. Please leave it in.
    ALOGI("AudioStream::open() rate = %d, channels = %d, format = %d, sharing = %d, dir = %s",
          mSampleRate, mSamplesPerFrame, mFormat, mSharingMode,
          (getDirection() == AAUDIO_DIRECTION_OUTPUT) ? "OUTPUT" : "INPUT");
    ALOGI("AudioStream::open() device = %d, perfMode = %d, callbackFrames = %d",
          mDeviceId, mPerformanceMode, mFramesPerDataCallback);

    // Check for values that are ridiculously out of range to prevent math overflow exploits.
    // The service will do a better check.
    if (mSamplesPerFrame < 0 || mSamplesPerFrame > 128) {
        ALOGE("AudioStream::open(): samplesPerFrame out of range = %d", mSamplesPerFrame);
        return AAUDIO_ERROR_OUT_OF_RANGE;
    }

    switch(mFormat) {
        case AAUDIO_FORMAT_UNSPECIFIED:
        case AAUDIO_FORMAT_PCM_I16:
        case AAUDIO_FORMAT_PCM_FLOAT:
            break; // valid
        default:
            ALOGE("AudioStream::open(): audioFormat not valid = %d", mFormat);
            return AAUDIO_ERROR_INVALID_FORMAT;
            // break;
    }

    if (mSampleRate != AAUDIO_UNSPECIFIED && (mSampleRate < 8000 || mSampleRate > 1000000)) {
        ALOGE("AudioStream::open(): mSampleRate out of range = %d", mSampleRate);
        return AAUDIO_ERROR_INVALID_RATE;
    }

    switch(mPerformanceMode) {
        case AAUDIO_PERFORMANCE_MODE_NONE:
        case AAUDIO_PERFORMANCE_MODE_POWER_SAVING:
        case AAUDIO_PERFORMANCE_MODE_LOW_LATENCY:
            break;
        default:
            ALOGE("AudioStream::open(): illegal performanceMode %d", mPerformanceMode);
            return AAUDIO_ERROR_ILLEGAL_ARGUMENT;
    }

    return AAUDIO_OK;
}

AudioStream::~AudioStream() {
    close();
}

aaudio_result_t AudioStream::waitForStateChange(aaudio_stream_state_t currentState,
                                                aaudio_stream_state_t *nextState,
                                                int64_t timeoutNanoseconds)
{
    aaudio_result_t result = updateStateWhileWaiting();
    if (result != AAUDIO_OK) {
        return result;
    }

    int64_t durationNanos = 20 * AAUDIO_NANOS_PER_MILLISECOND; // arbitrary
    aaudio_stream_state_t state = getState();
    while (state == currentState && timeoutNanoseconds > 0) {
        if (durationNanos > timeoutNanoseconds) {
            durationNanos = timeoutNanoseconds;
        }
        AudioClock::sleepForNanos(durationNanos);
        timeoutNanoseconds -= durationNanos;

        aaudio_result_t result = updateStateWhileWaiting();
        if (result != AAUDIO_OK) {
            return result;
        }

        state = getState();
    }
    if (nextState != nullptr) {
        *nextState = state;
    }
    return (state == currentState) ? AAUDIO_ERROR_TIMEOUT : AAUDIO_OK;
}

// This registers the callback thread with the server before
// passing control to the app. This gives the server an opportunity to boost
// the thread's performance characteristics.
void* AudioStream::wrapUserThread() {
    void* procResult = nullptr;
    mThreadRegistrationResult = registerThread();
    if (mThreadRegistrationResult == AAUDIO_OK) {
        // Run callback loop. This may take a very long time.
        procResult = mThreadProc(mThreadArg);
        mThreadRegistrationResult = unregisterThread();
    }
    return procResult;
}

// This is the entry point for the new thread created by createThread().
// It converts the 'C' function call to a C++ method call.
static void* AudioStream_internalThreadProc(void* threadArg) {
    AudioStream *audioStream = (AudioStream *) threadArg;
    return audioStream->wrapUserThread();
}

// This is not exposed in the API.
// But it is still used internally to implement callbacks for MMAP mode.
aaudio_result_t AudioStream::createThread(int64_t periodNanoseconds,
                                     aaudio_audio_thread_proc_t threadProc,
                                     void* threadArg)
{
    if (mHasThread) {
        return AAUDIO_ERROR_INVALID_STATE;
    }
    if (threadProc == nullptr) {
        return AAUDIO_ERROR_NULL;
    }
    // Pass input parameters to the background thread.
    mThreadProc = threadProc;
    mThreadArg = threadArg;
    setPeriodNanoseconds(periodNanoseconds);
    int err = pthread_create(&mThread, nullptr, AudioStream_internalThreadProc, this);
    if (err != 0) {
        return AAudioConvert_androidToAAudioResult(-errno);
    } else {
        mHasThread = true;
        return AAUDIO_OK;
    }
}

aaudio_result_t AudioStream::joinThread(void** returnArg, int64_t timeoutNanoseconds)
{
    if (!mHasThread) {
        return AAUDIO_ERROR_INVALID_STATE;
    }
#if 0
    // TODO implement equivalent of pthread_timedjoin_np()
    struct timespec abstime;
    int err = pthread_timedjoin_np(mThread, returnArg, &abstime);
#else
    int err = pthread_join(mThread, returnArg);
#endif
    mHasThread = false;
    return err ? AAudioConvert_androidToAAudioResult(-errno) : mThreadRegistrationResult;
}

