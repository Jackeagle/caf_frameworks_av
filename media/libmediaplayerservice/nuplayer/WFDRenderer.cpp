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

#define LOG_NDEBUG 0
#define LOG_TAG "WFDRenderer"
#include <utils/Log.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include "WFDRenderer.h"
namespace android {

// static
const int64_t NuPlayer::WFDRenderer::kMinPositionUpdateDelayUs = 100000ll;

NuPlayer::WFDRenderer::WFDRenderer(
        const sp<MediaPlayerBase::AudioSink> &sink,
        const sp<AMessage> &notify)
    : Renderer(sink, notify),
      mAudioSink(sink),
      mNotify(notify),
      mNumFramesWritten(0),
      mDrainAudioQueuePending(false),
      mDrainVideoQueuePending(false),
      mAudioQueueGeneration(0),
      mVideoQueueGeneration(0),
      mAnchorTimeMediaUs(-1),
      mRefAudioMediaTimeUs(-1),
      mRefVideoMediaTime(-1),
      mAnchorTimeRealUs(-1),
      mFlushingAudio(false),
      mFlushingVideo(false),
      mHasAudio(false),
      mHasVideo(false),
      mSyncQueues(false),
      mPaused(false),
      mWasPaused(false),
      mLastPositionUpdateUs(-1ll),
      mVideoLateByUs(0ll),
      mStats(NULL),
      mWFDAudioTimeMaster(false),
      mMediaClockUs(0),
      mMediaTimeRead(false),
      mPauseDurationUs(0),
      mPauseMediaClockUs(0) {
}

NuPlayer::WFDRenderer::~WFDRenderer() {
    if(mStats != NULL) {
        mStats->logStatistics();
        mStats->logSyncLoss();
        mStats = NULL;
    }
}

void NuPlayer::WFDRenderer::queueBuffer(
        bool audio,
        const sp<ABuffer> &buffer,
        const sp<AMessage> &notifyConsumed) {
    sp<AMessage> msg = new AMessage(kWhatQueueBuffer, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setBuffer("buffer", buffer);
    msg->setMessage("notifyConsumed", notifyConsumed);
    msg->post();
}

void NuPlayer::WFDRenderer::queueEOS(bool audio, status_t finalResult) {
    CHECK_NE(finalResult, (status_t)OK);

    if(mSyncQueues)
      wfdSyncQueuesDone();

    sp<AMessage> msg = new AMessage(kWhatQueueEOS, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setInt32("finalResult", finalResult);
    msg->post();
}

void NuPlayer::WFDRenderer::flush(bool audio) {
    Mutex::Autolock autoLock(mFlushLock);
    if (audio) {
        CHECK(!mFlushingAudio);
        mFlushingAudio = true;
    } else {
        CHECK(!mFlushingVideo);
        mFlushingVideo = true;
    }

    sp<AMessage> msg = new AMessage(kWhatFlush, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->post();
}

void NuPlayer::WFDRenderer::signalTimeDiscontinuity() {
    CHECK(mAudioQueue.empty());
    CHECK(mVideoQueue.empty());
    mAnchorTimeMediaUs = -1;
    mAnchorTimeRealUs = -1;
    mWasPaused = false;
    mSyncQueues = mHasAudio && mHasVideo;
    ALOGI("signalTimeDiscontinuity mHasAudio %d mHasVideo %d mSyncQueues %d",mHasAudio,mHasVideo,mSyncQueues);
}

void NuPlayer::WFDRenderer::pause() {
    (new AMessage(kWhatPause, id()))->post();
}

void NuPlayer::WFDRenderer::resume() {
    (new AMessage(kWhatResume, id()))->post();
}

void NuPlayer::WFDRenderer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatDrainAudioQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mAudioQueueGeneration) {
                break;
            }

            mDrainAudioQueuePending = false;

            if (wfdOnDrainAudioQueue()) {
                if (mWFDAudioTimeMaster == true) {
                    uint32_t numFramesPlayed;
                    CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed),
                             (status_t)OK);

                    uint32_t numFramesPendingPlayout =
                        mNumFramesWritten - numFramesPlayed;

                    // This is how long the audio sink will have data to
                    // play back.
                    int64_t delayUs =
                        mAudioSink->msecsPerFrame()
                            * numFramesPendingPlayout * 1000ll;

                    // Let's give it more data after about half that time
                    // has elapsed.
                    wfdPostDrainAudioQueue(delayUs / 2);
                }
                else
                {
                     wfdPostDrainAudioQueue();
                }
            }
            break;
        }

        case kWhatDrainVideoQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mVideoQueueGeneration) {
                break;
            }

            mDrainVideoQueuePending = false;

            wfdOnDrainVideoQueue();

            wfdPostDrainVideoQueue();
            break;
        }

        case kWhatQueueBuffer:
        {
            onQueueBuffer(msg);
            break;
        }

        case kWhatQueueEOS:
        {
            wfdOnQueueEOS(msg);
            break;
        }

        case kWhatFlush:
        {
            wfdOnFlush(msg);
            break;
        }

        case kWhatAudioSinkChanged:
        {
            wfdOnAudioSinkChanged();
            break;
        }

        case kWhatPause:
        {
            wfdOnPause();
            break;
        }

        case kWhatResume:
        {
            wfdOnResume();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void NuPlayer::WFDRenderer::wfdPostDrainAudioQueue(int64_t delayUs) {
    if (mDrainAudioQueuePending || mSyncQueues || mPaused) {
        return;
    }

    if (mAudioQueue.empty()) {
        return;
    }

   if(mWFDAudioTimeMaster == true) {
      mDrainAudioQueuePending = true;
      sp<AMessage> msg = new AMessage(kWhatDrainAudioQueue, id());
      msg->setInt32("generation", mAudioQueueGeneration);
      msg->post(delayUs);
   }
   else{
     QueueEntry &entry = *mAudioQueue.begin();
     mDrainAudioQueuePending = true;
     sp<AMessage> msg = new AMessage(kWhatDrainAudioQueue, id());
     msg->setInt32("generation", mAudioQueueGeneration);

     if (entry.mBuffer == NULL) {
       // EOS doesn't carry a timestamp.
       delayUs = 0;
     } else {
        int64_t mediaTimeUs;
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
        if ( (mHasAudio && !mHasVideo) && (mWasPaused == true)) {
             mWasPaused = false;
        }

        if (mRefAudioMediaTimeUs <0)
            mRefAudioMediaTimeUs = mediaTimeUs;
        mAnchorTimeMediaUs = mediaTimeUs - mRefAudioMediaTimeUs;
        mAnchorTimeRealUs = wfdGetMediaTime(true);

        int64_t realTimeUs = mediaTimeUs - WFD_RENDERER_AUDIO_LATENCY;
        //Audio latency, we need to fine tune this number  //(mAudioSink->latency());

        delayUs = mAnchorTimeMediaUs - wfdGetMediaTime(true);
        ALOGV("@@@@:: wfdPostDrainAudioQueue delayUs -  %lld us realTimeUs  %lld us   wfdGetMediaTime  %lld us ",
            delayUs, realTimeUs, wfdGetMediaTime(true));
        //need to evaluate this
        if(delayUs < 0) {
            delayUs = 0;
        }
     }
     /* Setting delay 2 Msec ahead */
     msg->post(delayUs - WFD_RENDERER_TIME_BEFORE_WAKE_UP);
  }
}

void NuPlayer::WFDRenderer::signalAudioSinkChanged() {
    (new AMessage(kWhatAudioSinkChanged, id()))->post();
}

bool NuPlayer::WFDRenderer::wfdOnDrainAudioQueue() {
    uint32_t numFramesPlayed;
    bool tooLate = false;
    /* To Do : Right now getPosition is crashing, once this is fixed we can use this */
    /*if (mAudioSink->getPosition(&numFramesPlayed) != OK) {
        return false;
    }

    ssize_t numFramesAvailableToWrite =
        mAudioSink->frameCount() - (mNumFramesWritten - numFramesPlayed);
   */
#if 0
    if (numFramesAvailableToWrite == mAudioSink->frameCount()) {
        ALOGI("audio sink underrun");
    } else {
        ALOGV("audio queue has %d frames left to play",
             mAudioSink->frameCount() - numFramesAvailableToWrite);
    }
#endif

    /* To Do : Right now getPosition is crashing, once this is fixed we can use this */
    /*  size_t numBytesAvailableToWrite =
        numFramesAvailableToWrite * mAudioSink->frameSize(); */

       QueueEntry *entry = &*mAudioQueue.begin();
       if (entry->mBuffer == NULL) {
            // EOS
            wfdNotifyEOS(true /* audio */, entry->mFinalResult);
            mAudioQueue.erase(mAudioQueue.begin());
            entry = NULL;
            mAudioLateByUs = 0ll;
            return false;
        }

        if (entry->mOffset == 0) {
            int64_t mediaTimeUs;
            CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
            if (mWFDAudioTimeMaster == true) {

                mAnchorTimeMediaUs = mediaTimeUs;

                uint32_t numFramesPlayed;
                CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed), (status_t)OK);

                uint32_t numFramesPendingPlayout =
                    mNumFramesWritten - numFramesPlayed;

                int64_t realTimeOffsetUs =
                    (mAudioSink->latency() / 2
                        + numFramesPendingPlayout
                            * mAudioSink->msecsPerFrame()) * 1000ll;

                mAnchorTimeRealUs =
                    wfdGetMediaTime(true) + realTimeOffsetUs;
            }
            else  {
               /* Right now mAudioSink is not giving proper latency,
               * once that is fixed we can get rid of this
               */
              int64_t realTimeUs = (mediaTimeUs -mRefAudioMediaTimeUs) - WFD_RENDERER_AUDIO_LATENCY;//(mAudioSink->latency());

              int64_t nowUs = wfdGetMediaTime(true);
              mAudioLateByUs = nowUs - realTimeUs;
              tooLate = (mAudioLateByUs > WFD_RENDERER_AVSYNC_WINDOW);
              ALOGV("@@@@:: wfdOnDrainAudioQueue mediaTimeUs  %lld us nowUs  %lld us  realTimeUs %lld us   mAudioLateByUs  %lld us ", mediaTimeUs, nowUs, realTimeUs, mAudioLateByUs);

              if (tooLate) {
                 ALOGV("Audio late by %lld us (%.2f secs)",
                 mAudioLateByUs, mAudioLateByUs / 1E6);
                 if(mStats != NULL) {
                 mStats->recordLate(realTimeUs,nowUs,mAudioLateByUs,realTimeUs);
              }
              else {
                ALOGV("rendering Audio  at media time %.2f secs", mediaTimeUs / 1E6);
                if(mStats != NULL) {
                   mStats->recordOnTime(realTimeUs,nowUs,mAudioLateByUs);
                }
              }
            }

            size_t copy = entry->mBuffer->size() - entry->mOffset;
            /* To Do : Right now getPosition is crashing, once this is fixed
             * we can use this
            */
            /*if (copy > numBytesAvailableToWrite) {
                  copy = numBytesAvailableToWrite;
              }
            */
            /* Giving the audio buffer only the frame is not too late */
            if (!tooLate) {
                ALOGV("@@@@:: writing audio buffer frame is not too late");
                CHECK_EQ(mAudioSink->write(
                        entry->mBuffer->data() + entry->mOffset, copy),
                       (ssize_t)copy);
            }
            else {
                ALOGV("@@@@:: Dropping audio buffer frame is too late by % lld us", mAudioLateByUs);
            }

            entry->mOffset += copy;
            if (entry->mOffset == entry->mBuffer->size()) {
                //entry->mNotifyConsumed->setInt32("render", !tooLate);
                //entry->mNotifyConsumed->post();
                ALOGV("@@@@:: in wfdOnDrainAudioQueue before erasing frame from mAudioQueue" );
                mAudioQueue.erase(mAudioQueue.begin());

                entry = NULL;
            }

            //numBytesAvailableToWrite -= copy;
            size_t copiedFrames = copy / mAudioSink->frameSize();
           mNumFramesWritten += copiedFrames;
           //no need to notify back for audio
           //wfdNotifyPosition();
         }
     }
  return true;
}


void NuPlayer::WFDRenderer::wfdPostDrainVideoQueue() {
    if (mDrainVideoQueuePending || mSyncQueues || mPaused) {
        return;
    }

    if (mVideoQueue.empty()) {
        return;
    }

    QueueEntry &entry = *mVideoQueue.begin();

    sp<AMessage> msg = new AMessage(kWhatDrainVideoQueue, id());
    msg->setInt32("generation", mVideoQueueGeneration);

    int64_t delayUs;

    if (entry.mBuffer == NULL) {
        // EOS doesn't carry a timestamp.
        delayUs = 0;
    } else {
        int64_t mediaTimeUs;
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

        if (mAnchorTimeMediaUs < 0) {
            delayUs = 0;

           if (!mMediaTimeRead)
            {
            if (mRefVideoMediaTime <0) {
                mRefVideoMediaTime = mediaTimeUs;
                }
            }
           else
            {
              mRefVideoMediaTime = mRefAudioMediaTimeUs;
            }

            if (!mHasAudio) {
                mAnchorTimeMediaUs = mediaTimeUs;
                mAnchorTimeRealUs = wfdGetMediaTime(false);
            }
        } else {
            if ( (!mHasAudio && mHasVideo) && (mWasPaused == true))
            {
               mAnchorTimeMediaUs = mediaTimeUs;
               mAnchorTimeRealUs = wfdGetMediaTime(false);
               mWasPaused = false;
            }
            if (mRefVideoMediaTime <0) {
                mRefVideoMediaTime = mediaTimeUs;
            }

          if (!mMediaTimeRead)
            {
            if (mRefVideoMediaTime <0) {
                mRefVideoMediaTime = mediaTimeUs;
            }
            }
           else
            {
              mRefVideoMediaTime = mRefAudioMediaTimeUs;
            }

            int64_t realTimeUs = mediaTimeUs - mRefVideoMediaTime;
            delayUs = realTimeUs - (wfdGetMediaTime(false));
            ALOGV("@@@@:: wfdPostDrainVideoQueue delay  %lld us  mediaTimeUs %lld us   wfdGetMediaTime()  %lld us   mRefVideoMediaTime   %lld us   ", delayUs, mediaTimeUs, wfdGetMediaTime(false),mRefVideoMediaTime);
        }
    }
    if(delayUs < 0) {
        delayUs = 0;
    }
    /* Setting delay 2 Msec ahead */
    msg->post(delayUs - WFD_RENDERER_TIME_BEFORE_WAKE_UP);
    mDrainVideoQueuePending = true;
}

void NuPlayer::WFDRenderer::wfdOnDrainVideoQueue() {
    if (mVideoQueue.empty()) {
        return;
    }
    QueueEntry *entry = &*mVideoQueue.begin();

    if (entry->mBuffer == NULL) {
        // EOS

        wfdNotifyEOS(false /* audio */, entry->mFinalResult);

        mVideoQueue.erase(mVideoQueue.begin());
        entry = NULL;

        mVideoLateByUs = 0ll;

        wfdNotifyPosition();
        return;
    }

    if(mStats != NULL) {
        mStats->logFps();
    }

    int64_t mediaTimeUs;
    CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
    int64_t realTimeUs=0;
    if (mWFDAudioTimeMaster == true) {
        realTimeUs = mediaTimeUs - mAnchorTimeMediaUs + mAnchorTimeRealUs;
    }
    else {
        realTimeUs = mediaTimeUs - mRefVideoMediaTime;
    }
    int64_t nowUs = wfdGetMediaTime(false);
    mVideoLateByUs = nowUs - realTimeUs;

    bool tooLate = (mVideoLateByUs > WFD_RENDERER_AVSYNC_WINDOW);
    ALOGV("@@@@:: wfdOnDrainVideoQueue mediaTimeUs %lld us \
           realTimeUs %lld us nowUs  %lld us  mVideoLateByUs  %lld us",\
           mediaTimeUs, realTimeUs, nowUs, mVideoLateByUs);

    if (tooLate) {
        ALOGV("video late by %lld us (%.2f secs)",
             mVideoLateByUs, mVideoLateByUs / 1E6);
        if(mStats != NULL) {
            mStats->recordLate(realTimeUs,nowUs,mVideoLateByUs,mAnchorTimeRealUs);
        }
    } else {
        ALOGV("rendering video at media time %.2f secs", mediaTimeUs / 1E6);
        if(mStats != NULL) {
            mStats->recordOnTime(realTimeUs,nowUs,mVideoLateByUs);
        }
    }

    entry->mNotifyConsumed->setInt32("render", !tooLate);
    entry->mNotifyConsumed->post();
    mVideoQueue.erase(mVideoQueue.begin());
    entry = NULL;
    // To Do: Right now crashing, yet to fix this
    //wfdNotifyPosition();
}

void NuPlayer::WFDRenderer::wfdNotifyEOS(bool audio, status_t finalResult) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->setInt32("finalResult", finalResult);
    notify->post();
}

void NuPlayer::WFDRenderer::onQueueBuffer(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (audio) {
        mHasAudio = true;
    } else {
        mHasVideo = true;
    }

    if (wfdDropBufferWhileFlushing(audio, msg)) {
        return;
    }

    sp<ABuffer> buffer;
    CHECK(msg->findBuffer("buffer", &buffer));

    sp<AMessage> notifyConsumed;
    CHECK(msg->findMessage("notifyConsumed", &notifyConsumed));

    QueueEntry entry;
    entry.mBuffer = buffer;
    entry.mNotifyConsumed = notifyConsumed;
    entry.mOffset = 0;
    entry.mFinalResult = OK;

    if (audio) {
        mAudioQueue.push_back(entry);
        wfdPostDrainAudioQueue();
    } else {
        mVideoQueue.push_back(entry);
        wfdPostDrainVideoQueue();
    }

    if (!mSyncQueues || mAudioQueue.empty() || mVideoQueue.empty()) {
        return;
    }

    sp<ABuffer> firstAudioBuffer = (*mAudioQueue.begin()).mBuffer;
    sp<ABuffer> firstVideoBuffer = (*mVideoQueue.begin()).mBuffer;

    if (firstAudioBuffer == NULL || firstVideoBuffer == NULL) {
        // EOS signalled on either queue.
        wfdSyncQueuesDone();
        return;
    }

    int64_t firstAudioTimeUs;
    int64_t firstVideoTimeUs;
    CHECK(firstAudioBuffer->meta()
            ->findInt64("timeUs", &firstAudioTimeUs));
    CHECK(firstVideoBuffer->meta()
            ->findInt64("timeUs", &firstVideoTimeUs));

    int64_t diff = firstVideoTimeUs - firstAudioTimeUs;

    ALOGV("queueDiff = %.2f secs", diff / 1E6);

    if (diff > 100000ll) {
        // Audio data starts More than 0.1 secs before video.
        // Drop some audio.

        (*mAudioQueue.begin()).mNotifyConsumed->post();
        mAudioQueue.erase(mAudioQueue.begin());
        return;
    }

    wfdSyncQueuesDone();
}

void NuPlayer::WFDRenderer::wfdSyncQueuesDone() {
     if (!mSyncQueues) {
        return;
    }

    mSyncQueues = false;

    if (!mAudioQueue.empty()) {
        wfdPostDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
        wfdPostDrainVideoQueue();
    }
}

void NuPlayer::WFDRenderer::wfdOnQueueEOS(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (wfdDropBufferWhileFlushing(audio, msg)) {
        return;
    }

    int32_t finalResult;
    CHECK(msg->findInt32("finalResult", &finalResult));

    QueueEntry entry;
    entry.mOffset = 0;
    entry.mFinalResult = finalResult;

    if (audio) {
        mAudioQueue.push_back(entry);
        wfdPostDrainAudioQueue();
    } else {
        mVideoQueue.push_back(entry);
        wfdPostDrainVideoQueue();
    }
}

void NuPlayer::WFDRenderer::wfdOnFlush(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    // If we're currently syncing the queues, i.e. dropping audio while
    // aligning the first audio/video buffer times and only one of the
    // two queues has data, we may starve that queue by not requesting
    // more buffers from the decoder. If the other source then encounters
    // a discontinuity that leads to flushing, we'll never find the
    // corresponding discontinuity on the other queue.
    // Therefore we'll stop syncing the queues if at least one of them
    // is flushed.
    wfdSyncQueuesDone();

    if (audio) {
        wfdFlushQueue(&mAudioQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingAudio = false;

        mDrainAudioQueuePending = false;
        ++mAudioQueueGeneration;
    } else {
        wfdFlushQueue(&mVideoQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingVideo = false;

        mDrainVideoQueuePending = false;
        ++mVideoQueueGeneration;
        if(mStats != NULL) {
            mStats->setVeryFirstFrame(true);
        }
    }
    wfdNotifyFlushComplete(audio);
}

void NuPlayer::WFDRenderer::wfdFlushQueue(List<QueueEntry> *queue) {
    while (!queue->empty()) {
        QueueEntry *entry = &*queue->begin();

        if (entry->mBuffer != NULL) {
            entry->mNotifyConsumed->post();
        }

        queue->erase(queue->begin());
        entry = NULL;
    }
}

void NuPlayer::WFDRenderer::wfdNotifyFlushComplete(bool audio) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushComplete);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->post();
}

bool NuPlayer::WFDRenderer::wfdDropBufferWhileFlushing(
        bool audio, const sp<AMessage> &msg) {
    bool flushing = false;

    {
        Mutex::Autolock autoLock(mFlushLock);
        if (audio) {
            flushing = mFlushingAudio;
        } else {
            flushing = mFlushingVideo;
        }
    }

    if (!flushing) {
        return false;
    }

    sp<AMessage> notifyConsumed;
    if (msg->findMessage("notifyConsumed", &notifyConsumed)) {
        notifyConsumed->post();
    }

    return true;
}

void NuPlayer::WFDRenderer::wfdOnAudioSinkChanged() {
    CHECK(!mDrainAudioQueuePending);
    mNumFramesWritten = 0;
    uint32_t written;
    if (mAudioSink->getFramesWritten(&written) == OK) {
        mNumFramesWritten = written;
    }
}

void NuPlayer::WFDRenderer::wfdNotifyPosition() {
    if (mAnchorTimeRealUs < 0 || mAnchorTimeMediaUs < 0) {
        return;
    }
    int64_t nowUs = wfdGetMediaTime(false);

    if (mLastPositionUpdateUs >= 0
            && nowUs < mLastPositionUpdateUs + kMinPositionUpdateDelayUs) {
        return;
    }
    mLastPositionUpdateUs = nowUs;

    int64_t positionUs = (nowUs - mAnchorTimeRealUs) + mAnchorTimeMediaUs;

    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatPosition);
    notify->setInt64("positionUs", positionUs);
    notify->setInt64("videoLateByUs", mVideoLateByUs);
    notify->post();
}

void NuPlayer::WFDRenderer::wfdOnPause() {
    CHECK(!mPaused);

    mDrainAudioQueuePending = false;
    ++mAudioQueueGeneration;

    mDrainVideoQueuePending = false;
    ++mVideoQueueGeneration;


    if (mHasAudio) {
        mAudioSink->pause();
    }

    ALOGV("now paused audio queue has %d entries, video has %d entries",
          mAudioQueue.size(), mVideoQueue.size());

    mPaused = true;
    mWasPaused = true;

    if(mStats != NULL) {
        int64_t positionUs;
        if(mAnchorTimeRealUs < 0 || mAnchorTimeMediaUs < 0) {
            positionUs = -1000;
        } else {
            int64_t nowUs = wfdGetMediaTime(true);
            positionUs = (nowUs - mAnchorTimeRealUs) + mAnchorTimeMediaUs;
        }

        mStats->logPause(positionUs);
    }
    mPauseMediaClockUs = wfdGetMediaTime(true);
}

void NuPlayer::WFDRenderer::wfdOnResume() {
    if (!mPaused) {
        return;
    }

//    mRefVideoMediaTime = -1;
//    mRefAudioMediaTimeUs = -1;
//    mMediaTimeRead = false;
/*
    if (mHasAudio) {
        mAudioSink->start();
    }
*/

    mPaused = false;

/*    if(mPauseMediaClockUs > 0){
        mPauseDurationUs += wfdGetMediaTime(true) - mPauseMediaClockUs;
        //Resetting the mPauseMediaClockUs
        mPauseMediaClockUs = 0;
    }*/

    if (!mAudioQueue.empty()) {
        wfdPostDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
        wfdPostDrainVideoQueue();
    }
}

void NuPlayer::WFDRenderer::registerStats(sp<NuPlayerStats> stats) {
    if(mStats != NULL) {
        mStats = NULL;
    }
    mStats = stats;
}

status_t NuPlayer::WFDRenderer::setMediaPresence(bool audio, bool bValue)
{
   if (audio)
   {
      ALOGV("mHasAudio set to %d from %d",bValue,mHasAudio);
      mHasAudio = bValue;
   }
   else
   {
     ALOGV("mHasVideo set to %d from %d",bValue,mHasVideo);
     mHasVideo = bValue;
   }
   return OK;
}

int64_t NuPlayer::WFDRenderer::wfdGetMediaTime(bool audio)
{
   int64_t mediaTimeClockUs = 0;
   if(mMediaTimeRead == false) {
     mMediaClockUs = ALooper::GetNowUs();
     mMediaTimeRead = true;
     return 0;
   } else {
     if(mPauseDurationUs > 0) {
          mediaTimeClockUs = ALooper::GetNowUs() - mMediaClockUs - mPauseDurationUs;
     } else {
          mediaTimeClockUs = ALooper::GetNowUs() - mMediaClockUs;
     }
   }
   return mediaTimeClockUs;
}

}  // namespace android
