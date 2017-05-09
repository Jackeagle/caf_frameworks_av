/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <cassert>
#include <aaudio/AAudio.h>

#include "AudioEndpointParcelable.h"
#include "AudioEndpoint.h"
#include "AAudioServiceMessage.h"

using namespace android;
using namespace aaudio;

AudioEndpoint::AudioEndpoint()
    : mOutputFreeRunning(false)
    , mDataReadCounter(0)
    , mDataWriteCounter(0)
{
}

AudioEndpoint::~AudioEndpoint()
{
}

static aaudio_result_t AudioEndpoint_validateQueueDescriptor(const char *type,
                                                  const RingBufferDescriptor *descriptor) {
    if (descriptor == nullptr) {
        ALOGE("AudioEndpoint_validateQueueDescriptor() NULL descriptor");
        return AAUDIO_ERROR_NULL;
    }
    if (descriptor->capacityInFrames <= 0) {
        ALOGE("AudioEndpoint_validateQueueDescriptor() bad capacityInFrames = %d",
              descriptor->capacityInFrames);
        return AAUDIO_ERROR_OUT_OF_RANGE;
    }
    if (descriptor->bytesPerFrame <= 1) {
        ALOGE("AudioEndpoint_validateQueueDescriptor() bad bytesPerFrame = %d",
              descriptor->bytesPerFrame);
        return AAUDIO_ERROR_OUT_OF_RANGE;
    }
    if (descriptor->dataAddress == nullptr) {
        ALOGE("AudioEndpoint_validateQueueDescriptor() NULL dataAddress");
        return AAUDIO_ERROR_NULL;
    }
    ALOGV("AudioEndpoint_validateQueueDescriptor %s, dataAddress at %p ====================",
          type,
          descriptor->dataAddress);
    ALOGV("AudioEndpoint_validateQueueDescriptor  readCounter at %p, writeCounter at %p",
          descriptor->readCounterAddress,
          descriptor->writeCounterAddress);

    // Try to READ from the data area.
    // This code will crash if the mmap failed.
    uint8_t value = descriptor->dataAddress[0];
    ALOGV("AudioEndpoint_validateQueueDescriptor() dataAddress[0] = %d, then try to write",
        (int) value);
    // Try to WRITE to the data area.
    descriptor->dataAddress[0] = value * 3;
    ALOGV("AudioEndpoint_validateQueueDescriptor() wrote successfully");

    if (descriptor->readCounterAddress) {
        fifo_counter_t counter = *descriptor->readCounterAddress;
        ALOGV("AudioEndpoint_validateQueueDescriptor() *readCounterAddress = %d, now write",
              (int) counter);
        *descriptor->readCounterAddress = counter;
        ALOGV("AudioEndpoint_validateQueueDescriptor() wrote readCounterAddress successfully");
    }
    if (descriptor->writeCounterAddress) {
        fifo_counter_t counter = *descriptor->writeCounterAddress;
        ALOGV("AudioEndpoint_validateQueueDescriptor() *writeCounterAddress = %d, now write",
              (int) counter);
        *descriptor->writeCounterAddress = counter;
        ALOGV("AudioEndpoint_validateQueueDescriptor() wrote writeCounterAddress successfully");
    }
    return AAUDIO_OK;
}

aaudio_result_t AudioEndpoint_validateDescriptor(const EndpointDescriptor *pEndpointDescriptor) {
    aaudio_result_t result = AudioEndpoint_validateQueueDescriptor("messages",
                                    &pEndpointDescriptor->upMessageQueueDescriptor);
    if (result == AAUDIO_OK) {
        result = AudioEndpoint_validateQueueDescriptor("data",
                                                &pEndpointDescriptor->downDataQueueDescriptor);
    }
    return result;
}

aaudio_result_t AudioEndpoint::configure(const EndpointDescriptor *pEndpointDescriptor)
{
    // TODO maybe remove after debugging
    aaudio_result_t result = AudioEndpoint_validateDescriptor(pEndpointDescriptor);
    if (result != AAUDIO_OK) {
        ALOGE("AudioEndpoint_validateQueueDescriptor returned %d %s",
              result, AAudio_convertResultToText(result));
        return result;
    }

    const RingBufferDescriptor *descriptor = &pEndpointDescriptor->upMessageQueueDescriptor;
    assert(descriptor->bytesPerFrame == sizeof(AAudioServiceMessage));
    assert(descriptor->readCounterAddress != nullptr);
    assert(descriptor->writeCounterAddress != nullptr);
    mUpCommandQueue = new FifoBuffer(
            descriptor->bytesPerFrame,
            descriptor->capacityInFrames,
            descriptor->readCounterAddress,
            descriptor->writeCounterAddress,
            descriptor->dataAddress
    );
    /* TODO mDownCommandQueue
    if (descriptor->capacityInFrames > 0) {
        descriptor = &pEndpointDescriptor->downMessageQueueDescriptor;
        mDownCommandQueue = new FifoBuffer(
                descriptor->capacityInFrames,
                descriptor->bytesPerFrame,
                descriptor->readCounterAddress,
                descriptor->writeCounterAddress,
                descriptor->dataAddress
        );
    }
     */
    descriptor = &pEndpointDescriptor->downDataQueueDescriptor;
    assert(descriptor->capacityInFrames > 0);
    assert(descriptor->bytesPerFrame > 1);
    assert(descriptor->bytesPerFrame < 4 * 16); // FIXME just for initial debugging
    assert(descriptor->framesPerBurst > 0);
    assert(descriptor->framesPerBurst < 8 * 1024); // FIXME just for initial debugging
    assert(descriptor->dataAddress != nullptr);
    ALOGV("AudioEndpoint::configure() data framesPerBurst = %d", descriptor->framesPerBurst);
    ALOGV("AudioEndpoint::configure() data readCounterAddress = %p", descriptor->readCounterAddress);
    mOutputFreeRunning = descriptor->readCounterAddress == nullptr;
    ALOGV("AudioEndpoint::configure() mOutputFreeRunning = %d", mOutputFreeRunning ? 1 : 0);
    int64_t *readCounterAddress = (descriptor->readCounterAddress == nullptr)
                                  ? &mDataReadCounter
                                  : descriptor->readCounterAddress;
    int64_t *writeCounterAddress = (descriptor->writeCounterAddress == nullptr)
                                  ? &mDataWriteCounter
                                  : descriptor->writeCounterAddress;

    mDownDataQueue = new FifoBuffer(
            descriptor->bytesPerFrame,
            descriptor->capacityInFrames,
            readCounterAddress,
            writeCounterAddress,
            descriptor->dataAddress
    );
    uint32_t threshold = descriptor->capacityInFrames / 2;
    mDownDataQueue->setThreshold(threshold);
    return result;
}

aaudio_result_t AudioEndpoint::readUpCommand(AAudioServiceMessage *commandPtr)
{
    return mUpCommandQueue->read(commandPtr, 1);
}

aaudio_result_t AudioEndpoint::writeDataNow(const void *buffer, int32_t numFrames)
{
    // TODO Make it easier for the AAudioStreamInternal to scale floats and write shorts
    // TODO Similar to block adapter write through technique. Add a DataConverter.
    return mDownDataQueue->write(buffer, numFrames);
}

void AudioEndpoint::getEmptyRoomAvailable(WrappingBuffer *wrappingBuffer) {
    mDownDataQueue->getEmptyRoomAvailable(wrappingBuffer);
}

void AudioEndpoint::advanceWriteIndex(int32_t deltaFrames) {
    mDownDataQueue->getFifoControllerBase()->advanceWriteIndex(deltaFrames);
}

void AudioEndpoint::setDownDataReadCounter(fifo_counter_t framesRead)
{
    mDownDataQueue->setReadCounter(framesRead);
}

fifo_counter_t AudioEndpoint::getDownDataReadCounter()
{
    return mDownDataQueue->getReadCounter();
}

void AudioEndpoint::setDownDataWriteCounter(fifo_counter_t framesRead)
{
    mDownDataQueue->setWriteCounter(framesRead);
}

fifo_counter_t AudioEndpoint::getDownDataWriteCounter()
{
    return mDownDataQueue->getWriteCounter();
}

int32_t AudioEndpoint::setBufferSizeInFrames(int32_t requestedFrames,
                                            int32_t *actualFrames)
{
    if (requestedFrames < ENDPOINT_DATA_QUEUE_SIZE_MIN) {
        requestedFrames = ENDPOINT_DATA_QUEUE_SIZE_MIN;
    }
    mDownDataQueue->setThreshold(requestedFrames);
    *actualFrames = mDownDataQueue->getThreshold();
    return AAUDIO_OK;
}

int32_t AudioEndpoint::getBufferSizeInFrames() const
{
    return mDownDataQueue->getThreshold();
}

int32_t AudioEndpoint::getBufferCapacityInFrames() const
{
    return (int32_t)mDownDataQueue->getBufferCapacityInFrames();
}

int32_t AudioEndpoint::getFullFramesAvailable()
{
    return mDownDataQueue->getFifoControllerBase()->getFullFramesAvailable();
}
