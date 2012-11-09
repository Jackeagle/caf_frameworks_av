/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserved.
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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

#ifndef POSTPROC_H_
#define POSTPROC_H_

#include <ui/Region.h>
#include <android/native_window.h>
#include <MediaBuffer.h>
#include <GraphicBuffer.h>
#include <MediaSource.h>
#include <utils/threads.h>
#include <PostProcNativeWindow.h>
#include <linux/msm_ion.h>
#include <QComOMXMetadata.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <MetaData.h>
#include <MediaDebug.h>
#include <gralloc_priv.h>
#include <dlfcn.h>
#include <utils/Log.h>
#include <PostProcControllerInterface.h>
#include <OMX_IVCommon.h>
#include <cutils/properties.h>

#define ALIGN8K 8192
#define ALIGN4K 4096
#define ALIGN2K 2048
#define ALIGN128 128
#define ALIGN32 32
#define ALIGN16 16

#define ALIGN( num, to ) (((num) + (to-1)) & (~(to-1)))

#define POSTPROC_LOGI(x, ...) ALOGI(x, ##__VA_ARGS__)
#define POSTPROC_LOGV(x, ...) ALOGV(x, ##__VA_ARGS__)
#define POSTPROC_LOGE(x, ...) ALOGE(x, ##__VA_ARGS__)
#define POSTPROC_DERIVED_LOGI(x, ...) POSTPROC_LOGI("[%s] "x, mDerivedName, ##__VA_ARGS__)
#define POSTPROC_DERIVED_LOGV(x, ...) POSTPROC_LOGV("[%s] "x, mDerivedName, ##__VA_ARGS__)
#define POSTPROC_DERIVED_LOGE(x, ...) POSTPROC_LOGE("[%s] "x, mDerivedName, ##__VA_ARGS__)

using namespace android;

struct OutputBuffer
{
    MediaBuffer * buffer;
    bool isPostProcBuffer;
};

struct PostProcBuffer
{
    MediaBuffer * buffer;
    bool isFree;
};

typedef encoder_media_buffer_type post_proc_media_buffer_type;

namespace android {

struct PostProc : public MediaSource,
                  public MediaBufferObserver,
                  public PostProcControllerInterface {

public:
    virtual status_t init(sp<MediaSource> decoder, sp<PostProcNativeWindow> nativeWindow, const sp<MetaData> &meta, char *name = "");

public: // from MediaSource
    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();

    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

    virtual status_t pause();

public: // from MediaBufferObserver
    void signalBufferReturned(MediaBuffer *buffer);

public: // from PostProcControllerInterface
    status_t signalMessage(PostProcMessage *msg);

protected:
    virtual ~PostProc();
    virtual status_t postProcessBuffer(MediaBuffer *inputBuffer, MediaBuffer *outputBuffer) = 0;
    virtual status_t setBufferInfo(const sp<MetaData> &meta) = 0;
    virtual bool postProcessingPossible() = 0;

protected:
    sp<MediaSource> mSource;
    sp<PostProcNativeWindow> mNativeWindow;
    size_t mBufferSize;
    size_t mNumBuffers;
    sp<MetaData> mOutputFormat;
    size_t mHeight;
    size_t mWidth;
    size_t mSrcFormat;
    size_t mDstFormat;
    size_t mStride;
    size_t mSlice;
    int32_t mIonFd;

private:
    void createWorkerThread();
    void destroyWorkerThread();
    static void *threadEntry(void* me);
    void readLoop();

    // Funtions that are called from readLoop(need to acquire a lock):
    void checkForFirstFrame();
    bool needsPostProcessing();
    status_t findFreePostProcBuffer(MediaBuffer **buffer);
    void releasePostProcBuffer(MediaBuffer *buffer);
    void queueBuffer(MediaBuffer *buffer, bool isPostProcBuffer);

    void releaseInputBuffer(MediaBuffer *buffer); // does not need a lock

    // Helper functions that are called from other functions(that have a lock):
    status_t notifyNativeWindow();
    status_t dequeuePostProcBufferFromNativeWindow(MediaBuffer **buffer);
    status_t allocatePostProcBuffers();
    void releasePostProcBuffers();
    void releaseQueuedBuffers();
    void flushPostProcBuffer(MediaBuffer *buffer);
    MediaBuffer* getPostProcBufferByNativeHandle(ANativeWindowBuffer *buf);
    void pushPostProcBufferToFreeList(MediaBuffer *buffer);
    int allocateIonBuffer(uint32_t size, int *handle);
    status_t getInputFormat();
    void updateInputFormat();

    enum State {
        LOADED,
        EXECUTING,
        SEEKING,
        PAUSED,
        ERROR,
        STOPPED
    };

private:
    char * mDerivedName;
    pthread_t mThread;
    bool mDone;

    ReadOptions mReadOptions;
    status_t mReadError;

    Mutex mLock;
    Condition mOutputBufferCondition;
    Condition mPausedCondition;
    Condition mSeekCondition;
    Condition mPostProcBufferCondition;

    List<OutputBuffer *> mOutputBuffers;
    Vector<PostProcBuffer *> mPostProcBuffers;
    List<int> mFreePostProcBuffers;

    bool mLastBufferWasPostProcessedInThread;
    bool mLastBufferWasPostProcessedInRead;
    size_t mLastBufferFormat;
    State mState;

    bool mDoPostProcessing;
    PostProcMessage * mPostProcMsg;

    bool mFirstFrame;
    bool mPostProcessingEnabled;
};

}  // namespace android

#endif  // POSTPROC_H_

