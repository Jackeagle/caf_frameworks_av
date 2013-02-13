/*
 * Copyright (c) 2012, The Linux Foundation. All rights reserveds reserved
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Copyright (C) 2010 The Android Open Source Project
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


#ifndef WFD_RENDERER_H_
#define WFD_RENDERER_H_

#include "NuPlayerRenderer.h"

namespace android {

#define WFD_RENDERER_TIME_BEFORE_WAKE_UP 2000
/* Right now Audio sync is not giving proper latency,
 * once that is fixed we can get rid of this
*/
#define WFD_RENDERER_AUDIO_LATENCY 2000
/* Right now setting AV Sync window to 250 msec, ideal value is <40 msec
   there is a huge video startup delay compared to audio, based on rigirous testing
   we can fine tune this number
*/
#define WFD_RENDERER_AVSYNC_WINDOW 250000

struct ABuffer;

struct NuPlayer::WFDRenderer : public NuPlayer::Renderer {
    WFDRenderer(const sp<MediaPlayerBase::AudioSink> &sink,
             const sp<AMessage> &notify);

    void queueBuffer(
            bool audio,
            const sp<ABuffer> &buffer,
            const sp<AMessage> &notifyConsumed);

    virtual void queueEOS(bool audio, status_t finalResult);

    virtual void flush(bool audio);

    virtual void signalTimeDiscontinuity();

    virtual void signalAudioSinkChanged();

    virtual void pause();
    virtual void resume();

    void setBaseMediaTime(int64_t ts);

    enum {
        kWhatEOS                = 'eos ',
        kWhatFlushComplete      = 'fluC',
        kWhatPosition           = 'posi',
    };

protected:
    virtual ~WFDRenderer();

    virtual void onMessageReceived(const sp<AMessage> &msg);

private:
    enum {
        kWhatDrainAudioQueue    = 'draA',
        kWhatDrainVideoQueue    = 'draV',
        kWhatQueueBuffer        = 'queB',
        kWhatQueueEOS           = 'qEOS',
        kWhatFlush              = 'flus',
        kWhatAudioSinkChanged   = 'auSC',
        kWhatPause              = 'paus',
        kWhatResume             = 'resm',
    };

    struct QueueEntry {
        sp<ABuffer> mBuffer;
        sp<AMessage> mNotifyConsumed;
        size_t mOffset;
        status_t mFinalResult;
    };

    static const int64_t kMinPositionUpdateDelayUs;

    sp<MediaPlayerBase::AudioSink> mAudioSink;
    sp<AMessage> mNotify;
    List<QueueEntry> mAudioQueue;
    List<QueueEntry> mVideoQueue;
    uint32_t mNumFramesWritten;

    bool mDrainAudioQueuePending;
    bool mDrainVideoQueuePending;
    int32_t mAudioQueueGeneration;
    int32_t mVideoQueueGeneration;

    int64_t mAnchorTimeMediaUs;
    int64_t mAnchorTimeRealUs;

    int64_t mRefAudioMediaTimeUs;
    int64_t mRefVideoMediaTime;

    Mutex mFlushLock;  // protects the following 2 member vars.
    bool mFlushingAudio;
    bool mFlushingVideo;

    bool mHasAudio;
    bool mHasVideo;
    bool mSyncQueues;

    bool mPaused;
    bool mWasPaused; // if paused then store the info
    bool mWFDAudioTimeMaster;

    int64_t mLastPositionUpdateUs;
    int64_t mVideoLateByUs;
    int64_t mAudioLateByUs;

    int64_t mMediaClockUs;
    bool mMediaTimeRead;

    int64_t mPauseDurationUs;
    int64_t mPauseMediaClockUs;

    bool wfdOnDrainAudioQueue();
    void wfdPostDrainAudioQueue(int64_t delayUs = 0);

    void wfdOnDrainVideoQueue();
    void wfdPostDrainVideoQueue();

    virtual void onQueueBuffer(const sp<AMessage> &msg);
    void wfdOnQueueEOS(const sp<AMessage> &msg);
    void wfdOnFlush(const sp<AMessage> &msg);
    void wfdOnAudioSinkChanged();
    void wfdOnPause();
    void wfdOnResume();

    void wfdNotifyEOS(bool audio, status_t finalResult);
    void wfdNotifyFlushComplete(bool audio);
    void wfdNotifyPosition();
    void wfdNotifyVideoLateBy(int64_t lateByUs);

    void wfdFlushQueue(List<QueueEntry> *queue);
    bool wfdDropBufferWhileFlushing(bool audio, const sp<AMessage> &msg);
    void wfdSyncQueuesDone();
    int64_t wfdGetMediaTime(bool audio);

    // for qualcomm statistics profiling
  public:
    virtual void registerStats(sp<NuPlayerStats> stats);
    virtual status_t setMediaPresence(bool audio, bool bValue);
  private:
    sp<NuPlayerStats> mStats;

    DISALLOW_EVIL_CONSTRUCTORS(WFDRenderer);
};

}  // namespace android

#endif  // NUPLAYER_RENDERER_H_
