/*
 * Copyright (C) 2017 The Android Open Source Project
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

// Play sine waves using an AAudio callback.

#ifndef AAUDIO_SIMPLE_PLAYER_H
#define AAUDIO_SIMPLE_PLAYER_H

#include <unistd.h>
#include <sched.h>

#include <aaudio/AAudio.h>
#include "SineGenerator.h"

//#define SHARING_MODE  AAUDIO_SHARING_MODE_EXCLUSIVE
#define SHARING_MODE  AAUDIO_SHARING_MODE_SHARED
#define PERFORMANCE_MODE AAUDIO_PERFORMANCE_MODE_NONE

/**
 * Simple wrapper for AAudio that opens an output stream either in callback or blocking write mode.
 */
class AAudioSimplePlayer {
public:
    AAudioSimplePlayer() {}
    ~AAudioSimplePlayer() {
        close();
    };

    /**
     * Call this before calling open().
     * @param requestedSharingMode
     */
    void setSharingMode(aaudio_sharing_mode_t requestedSharingMode) {
        mRequestedSharingMode = requestedSharingMode;
    }

    /**
     * Call this before calling open().
     * @param requestedPerformanceMode
     */
    void setPerformanceMode(aaudio_performance_mode_t requestedPerformanceMode) {
        mRequestedPerformanceMode = requestedPerformanceMode;
    }

    /**
     * Also known as "sample rate"
     * Only call this after open() has been called.
     */
    int32_t getFramesPerSecond() {
        if (mStream == nullptr) {
            return AAUDIO_ERROR_INVALID_STATE;
        }
        return AAudioStream_getSampleRate(mStream);;
    }

    /**
     * Only call this after open() has been called.
     */
    int32_t getChannelCount() {
        if (mStream == nullptr) {
            return AAUDIO_ERROR_INVALID_STATE;
        }
        return AAudioStream_getChannelCount(mStream);;
    }

    /**
     * Open a stream
     */
    aaudio_result_t open(int channelCount, int sampSampleRate, aaudio_format_t format,
                         AAudioStream_dataCallback dataProc, AAudioStream_errorCallback errorProc,
                         void *userContext) {
        aaudio_result_t result = AAUDIO_OK;

        // Use an AAudioStreamBuilder to contain requested parameters.
        result = AAudio_createStreamBuilder(&mBuilder);
        if (result != AAUDIO_OK) return result;

        //AAudioStreamBuilder_setSampleRate(mBuilder, 44100);
        AAudioStreamBuilder_setPerformanceMode(mBuilder, mRequestedPerformanceMode);
        AAudioStreamBuilder_setSharingMode(mBuilder, mRequestedSharingMode);
        if (dataProc != nullptr) {
            AAudioStreamBuilder_setDataCallback(mBuilder, dataProc, userContext);
        }
        if (errorProc != nullptr) {
            AAudioStreamBuilder_setErrorCallback(mBuilder, errorProc, userContext);
        }
        AAudioStreamBuilder_setChannelCount(mBuilder, channelCount);
        AAudioStreamBuilder_setSampleRate(mBuilder, sampSampleRate);
        AAudioStreamBuilder_setFormat(mBuilder, format);
        //AAudioStreamBuilder_setFramesPerDataCallback(mBuilder, CALLBACK_SIZE_FRAMES);
        AAudioStreamBuilder_setBufferCapacityInFrames(mBuilder, 48 * 8);

        //aaudio_performance_mode_t perfMode = AAUDIO_PERFORMANCE_MODE_NONE;
        aaudio_performance_mode_t perfMode = AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
        //aaudio_performance_mode_t perfMode = AAUDIO_PERFORMANCE_MODE_POWER_SAVING;
        AAudioStreamBuilder_setPerformanceMode(mBuilder, perfMode);

        // Open an AAudioStream using the Builder.
        result = AAudioStreamBuilder_openStream(mBuilder, &mStream);
        if (result != AAUDIO_OK) goto finish1;

        printf("AAudioStream_getFramesPerBurst() = %d\n",
               AAudioStream_getFramesPerBurst(mStream));
        printf("AAudioStream_getBufferSizeInFrames() = %d\n",
               AAudioStream_getBufferSizeInFrames(mStream));
        printf("AAudioStream_getBufferCapacityInFrames() = %d\n",
               AAudioStream_getBufferCapacityInFrames(mStream));
        printf("AAudioStream_getPerformanceMode() = %d, requested %d\n",
               AAudioStream_getPerformanceMode(mStream), perfMode);

     finish1:
        AAudioStreamBuilder_delete(mBuilder);
        mBuilder = nullptr;
        return result;
    }

    aaudio_result_t close() {
        if (mStream != nullptr) {
            printf("call AAudioStream_close(%p)\n", mStream);  fflush(stdout);
            AAudioStream_close(mStream);
            mStream = nullptr;
            AAudioStreamBuilder_delete(mBuilder);
            mBuilder = nullptr;
        }
        return AAUDIO_OK;
    }

    // Write zero data to fill up the buffer and prevent underruns.
    aaudio_result_t prime() {
        int32_t samplesPerFrame = AAudioStream_getChannelCount(mStream);
        const int numFrames = 32;
        float zeros[numFrames * samplesPerFrame];
        memset(zeros, 0, sizeof(zeros));
        aaudio_result_t result = numFrames;
        while (result == numFrames) {
            result = AAudioStream_write(mStream, zeros, numFrames, 0);
        }
        return result;
    }

    // Start the stream. AAudio will start calling your callback function.
     aaudio_result_t start() {
        aaudio_result_t result = AAudioStream_requestStart(mStream);
        if (result != AAUDIO_OK) {
            printf("ERROR - AAudioStream_requestStart() returned %d %s\n",
                    result, AAudio_convertResultToText(result));
        }
        return result;
    }

    // Stop the stream. AAudio will stop calling your callback function.
    aaudio_result_t stop() {
        aaudio_result_t result = AAudioStream_requestStop(mStream);
        if (result != AAUDIO_OK) {
            printf("ERROR - AAudioStream_requestStop() returned %d %s\n",
                    result, AAudio_convertResultToText(result));
        }
        int32_t xRunCount = AAudioStream_getXRunCount(mStream);
        printf("AAudioStream_getXRunCount %d\n", xRunCount);
        return result;
    }

    AAudioStream *getStream() const {
        return mStream;
    }

private:
    AAudioStreamBuilder      *mBuilder = nullptr;
    AAudioStream             *mStream = nullptr;
    aaudio_sharing_mode_t     mRequestedSharingMode = SHARING_MODE;
    aaudio_performance_mode_t mRequestedPerformanceMode = PERFORMANCE_MODE;
};

typedef struct SineThreadedData_s {
    SineGenerator  sineOsc1;
    SineGenerator  sineOsc2;
    int            scheduler;
    bool           schedulerChecked;
} SineThreadedData_t;

// Callback function that fills the audio output buffer.
aaudio_data_callback_result_t SimplePlayerDataCallbackProc(
        AAudioStream *stream,
        void *userData,
        void *audioData,
        int32_t numFrames
        ) {

    // should not happen but just in case...
    if (userData == nullptr) {
        fprintf(stderr, "ERROR - SimplePlayerDataCallbackProc needs userData\n");
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    SineThreadedData_t *sineData = (SineThreadedData_t *) userData;

    if (!sineData->schedulerChecked) {
        sineData->scheduler = sched_getscheduler(gettid());
        sineData->schedulerChecked = true;
    }

    int32_t samplesPerFrame = AAudioStream_getChannelCount(stream);
    // This code only plays on the first one or two channels.
    // TODO Support arbitrary number of channels.
    switch (AAudioStream_getFormat(stream)) {
        case AAUDIO_FORMAT_PCM_I16: {
            int16_t *audioBuffer = (int16_t *) audioData;
            // Render sine waves as shorts to first channel.
            sineData->sineOsc1.render(&audioBuffer[0], samplesPerFrame, numFrames);
            // Render sine waves to second channel if there is one.
            if (samplesPerFrame > 1) {
                sineData->sineOsc2.render(&audioBuffer[1], samplesPerFrame, numFrames);
            }
        }
        break;
        case AAUDIO_FORMAT_PCM_FLOAT: {
            float *audioBuffer = (float *) audioData;
            // Render sine waves as floats to first channel.
            sineData->sineOsc1.render(&audioBuffer[0], samplesPerFrame, numFrames);
            // Render sine waves to second channel if there is one.
            if (samplesPerFrame > 1) {
                sineData->sineOsc2.render(&audioBuffer[1], samplesPerFrame, numFrames);
            }
        }
        break;
        default:
            return AAUDIO_CALLBACK_RESULT_STOP;
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void SimplePlayerErrorCallbackProc(
        AAudioStream *stream __unused,
        void *userData __unused,
        aaudio_result_t error)
{
    printf("Error Callback, error: %d\n",(int)error);
}

#endif //AAUDIO_SIMPLE_PLAYER_H
