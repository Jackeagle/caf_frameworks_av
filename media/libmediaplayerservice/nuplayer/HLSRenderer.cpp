/*
 * Copyright (c) 2014, The Linux Foundation. All rights reserveds reserved.
 * Not a Contribution.
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

//#define LOG_NDEBUG 0
#define LOG_TAG "HLSTunnelRenderer"
#include <utils/Log.h>

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include "HLSRenderer.h"

namespace android {

//AV Sync window in micro-seconds
#define TUNNEL_RENDERER_AV_SYNC_WINDOW 40000
//Minimum buffering threshold for audio(in micro-seconds) before throttling can trigger.
#define TUNNEL_AUDIO_MIN_DATA_LEAD		70000
// static
const int64_t NuPlayer::HLSRenderer::kMinPositionUpdateDelayUs = 100000ll;

NuPlayer::HLSRenderer::HLSRenderer(
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
      mAnchorTimeRealUs(-1),
      mFlushingAudio(false),
      mFlushingVideo(false),
      mHasAudio(false),
      mHasVideo(false),
      mSyncQueues(false),
      mPaused(false),
      mVideoRenderingStarted(false),
      mLastPositionUpdateUs(-1ll),
      mVideoLateByUs(0ll),
      mLastSentAudioSinkTSUs(-1) {
		  ALOGV("HLSRenderer::HLSRenderer TUNNEL_RENDERER_AV_SYNC_WINDOW %d",TUNNEL_RENDERER_AV_SYNC_WINDOW);
		  ALOGV("HLSRenderer::HLSRenderer TUNNEL_AUDIO_MIN_DATA_LEAD %d",TUNNEL_AUDIO_MIN_DATA_LEAD);
}

NuPlayer::HLSRenderer::~HLSRenderer() {
}

void NuPlayer::HLSRenderer::queueBuffer(
        bool audio,
        const sp<ABuffer> &buffer,
        const sp<AMessage> &notifyConsumed) {
    sp<AMessage> msg = new AMessage(kWhatQueueBuffer, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setBuffer("buffer", buffer);
    msg->setMessage("notifyConsumed", notifyConsumed);
    msg->post();
}

void NuPlayer::HLSRenderer::queueEOS(bool audio, status_t finalResult) {
    CHECK_NE(finalResult, (status_t)OK);
    ALOGV("HLSRenderer::queueEOS audio? %d",audio);

    sp<AMessage> msg = new AMessage(kWhatQueueEOS, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->setInt32("finalResult", finalResult);
    msg->post();
}

void NuPlayer::HLSRenderer::flush(bool audio) {
    {
        Mutex::Autolock autoLock(mFlushLock);
        if (audio) {
            CHECK(!mFlushingAudio);
            mFlushingAudio = true;
            ALOGV("HLSRenderer::flush audio");
        } else {
            CHECK(!mFlushingVideo);
            mFlushingVideo = true;
            ALOGV("HLSRenderer::flush video");
        }
    }

    sp<AMessage> msg = new AMessage(kWhatFlush, id());
    msg->setInt32("audio", static_cast<int32_t>(audio));
    msg->post();
}

void NuPlayer::HLSRenderer::signalTimeDiscontinuity() {
	ALOGV("HLSRenderer::signalTimeDiscontinuity");
	ALOGV("HLSRenderer::signalTimeDiscontinuity audio queue has %d entries, video has %d entries",
			mAudioQueue.size(), mVideoQueue.size());
    CHECK(mAudioQueue.empty());
    CHECK(mVideoQueue.empty());
    mAnchorTimeMediaUs = -1;
    mAnchorTimeRealUs = -1;
    mSyncQueues = mHasAudio && mHasVideo;
}

void NuPlayer::HLSRenderer::pause() {
    (new AMessage(kWhatPause, id()))->post();
}

void NuPlayer::HLSRenderer::resume() {
    (new AMessage(kWhatResume, id()))->post();
}

void NuPlayer::HLSRenderer::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatDrainAudioQueue:
        {
            int32_t generation;
            CHECK(msg->findInt32("generation", &generation));
            if (generation != mAudioQueueGeneration) {
                break;
            }

            mDrainAudioQueuePending = false;
            ALOGV("HLSRenderer::onMessageReceived kWhatDrainAudioQueue");

            if (onDrainAudioQueue()) {
#if 0
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
#else
				uint32_t audioTsMsec = 0;
				int64_t delayUs = 0;
				if(mAudioSink.get() != NULL) {
					if (mAudioSink->getPosition(&audioTsMsec) == OK) {
						ALOGV("HLSRenderer::onMessageReceived kWhatDrainAudioQueue mLastSentAudioSinkTSUs %lld audioTsMsec %d",
						mLastSentAudioSinkTSUs,audioTsMsec);
						delayUs = mLastSentAudioSinkTSUs - (audioTsMsec*1000);
						ALOGV("AudioDataLead %lld Us",delayUs);
						if(delayUs < TUNNEL_AUDIO_MIN_DATA_LEAD) {
							ALOGV("HLSRenderer::onMessageReceived kWhatDrainAudioQueue audioDataLead < TUNNEL_AUDIO_MIN_DATA_LEAD..not throttling...");
							delayUs = 0;
						}
					}
				}
#endif

                // Let's give it more data after about half that time
                // has elapsed.
                postDrainAudioQueue(delayUs / 2);
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
            ALOGV("HLSRenderer::onMessageReceived kWhatDrainVideoQueue");

            onDrainVideoQueue();

            postDrainVideoQueue();
            break;
        }

        case kWhatQueueBuffer:
        {
            onQueueBuffer(msg);
            break;
        }

        case kWhatQueueEOS:
        {
            onQueueEOS(msg);
            break;
        }

        case kWhatFlush:
        {
            onFlush(msg);
            break;
        }

        case kWhatAudioSinkChanged:
        {
            onAudioSinkChanged();
            break;
        }

        case kWhatPause:
        {
            onPause();
            break;
        }

        case kWhatResume:
        {
            onResume();
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void NuPlayer::HLSRenderer::postDrainAudioQueue(int64_t delayUs) {
	ALOGV("HLSRenderer::postDrainAudioQueue delayUs %lld",delayUs);
    if (mDrainAudioQueuePending || mSyncQueues || mPaused) {
		ALOGV("HLSRenderer::postDrainAudioQueue returning from (mDrainAudioQueuePending || mSyncQueues || mPaused)check");
        return;
    }

    if (mAudioQueue.empty()) {
		ALOGV("HLSRenderer::postDrainAudioQueue mAudioQueue is empty");
        return;
    }

    mDrainAudioQueuePending = true;
    sp<AMessage> msg = new AMessage(kWhatDrainAudioQueue, id());
    msg->setInt32("generation", mAudioQueueGeneration);
    ALOGV("HLSRenderer::postDrainAudioQueue posting kWhatDrainAudioQueue with delayUs %lld",delayUs);
    msg->post(delayUs);
}

void NuPlayer::HLSRenderer::signalAudioSinkChanged() {
    (new AMessage(kWhatAudioSinkChanged, id()))->post();
}

bool NuPlayer::HLSRenderer::onDrainAudioQueue() {
    uint32_t numFramesPlayed;
    if (mAudioSink->getPosition(&numFramesPlayed) != OK) {
        return false;
    }
	ALOGV("HLSRenderer::onDrainAudioQueue numFramesPlayed %d (TS in Msec)",numFramesPlayed);

#if 0
    ssize_t numFramesAvailableToWrite =
        mAudioSink->frameCount() - (mNumFramesWritten - numFramesPlayed);
#endif

#if 0
    if (numFramesAvailableToWrite == mAudioSink->frameCount()) {
        ALOGI("audio sink underrun");
    } else {
        ALOGV("audio queue has %d frames left to play",
             mAudioSink->frameCount() - numFramesAvailableToWrite);
    }
#endif

#if 0
    size_t numBytesAvailableToWrite =
        numFramesAvailableToWrite * mAudioSink->frameSize();

    while (numBytesAvailableToWrite > 0 && !mAudioQueue.empty()) {
        QueueEntry *entry = &*mAudioQueue.begin();

        if (entry->mBuffer == NULL) {
            // EOS

            notifyEOS(true /* audio */, entry->mFinalResult);

            mAudioQueue.erase(mAudioQueue.begin());
            entry = NULL;
            return false;
        }

        if (entry->mOffset == 0) {
            int64_t mediaTimeUs;
            CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));

            ALOGV("rendering audio at media time %.2f secs", mediaTimeUs / 1E6);

            mAnchorTimeMediaUs = mediaTimeUs;

            uint32_t numFramesPlayed;
            CHECK_EQ(mAudioSink->getPosition(&numFramesPlayed), (status_t)OK);

            uint32_t numFramesPendingPlayout =
                mNumFramesWritten - numFramesPlayed;

            int64_t realTimeOffsetUs =
                (mAudioSink->latency() / 2  /* XXX */
                    + numFramesPendingPlayout
                        * mAudioSink->msecsPerFrame()) * 1000ll;

            // ALOGI("realTimeOffsetUs = %lld us", realTimeOffsetUs);

            mAnchorTimeRealUs =
                ALooper::GetNowUs() + realTimeOffsetUs;
        }

        size_t copy = entry->mBuffer->size() - entry->mOffset;
        if (copy > numBytesAvailableToWrite) {
            copy = numBytesAvailableToWrite;
        }

        CHECK_EQ(mAudioSink->write(
                    entry->mBuffer->data() + entry->mOffset, copy),
                 (ssize_t)copy);

        entry->mOffset += copy;
        if (entry->mOffset == entry->mBuffer->size()) {
            entry->mNotifyConsumed->post();
            mAudioQueue.erase(mAudioQueue.begin());

            entry = NULL;
        }

        numBytesAvailableToWrite -= copy;
        size_t copiedFrames = copy / mAudioSink->frameSize();
        mNumFramesWritten += copiedFrames;
    }
#else
		QueueEntry *entry = &*mAudioQueue.begin();
		if (entry->mBuffer == NULL) {
			// EOS
			notifyEOS(true /* audio */, entry->mFinalResult);
			mAudioQueue.erase(mAudioQueue.begin());
			entry = NULL;
			ALOGV("Audio EOS..");
			return false;
		}
		int64_t mediaTimeUs = 0;
		if (entry->mOffset == 0) {
			CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
			ALOGV("Received audio buffer with timeUs %lld", mediaTimeUs);
			uint32_t nCurrAudioTsMsec;
			CHECK_EQ(mAudioSink->getPosition(&nCurrAudioTsMsec), (status_t)OK);
			mAnchorTimeMediaUs = (nCurrAudioTsMsec*1000);
			ALOGV("onDrainAudioQueue mAnchorTimeMediaUs %lld nCurrAudioTsMsec %d",mAnchorTimeMediaUs,nCurrAudioTsMsec);
			int64_t nAudioLeadInMsec = (mediaTimeUs/1000) - nCurrAudioTsMsec;
			ALOGV("onDrainAudioQueue nAudioLeadInMsec %lld",nAudioLeadInMsec);
			ALOGV("onDrainAudioQueue  mAudioSink->latency() %d",mAudioSink->latency());

			int64_t realTimeOffsetUs =
				(mAudioSink->latency() / 2  /* XXX */
				+ nAudioLeadInMsec) * 1000ll;
			ALOGV("onDrainAudioQueue realTimeOffsetUs %lld",realTimeOffsetUs);
			int64_t clock = ALooper::GetNowUs();
			ALOGV("onDrainAudioQueue clock %lld",clock);
			//mAnchorTimeRealUs = clock + realTimeOffsetUs;
			//ALOGV("onDrainAudioQueue mAnchorTimeRealUs =clock + realTimeOffsetUs %lld",mAnchorTimeRealUs);
			mAnchorTimeRealUs = clock;
			ALOGV("onDrainAudioQueue mAnchorTimeRealUs =ALooper::GetNowUs() %lld",mAnchorTimeRealUs);
		}
        size_t copy = entry->mBuffer->size() - entry->mOffset;
        ALOGV("onDrainAudioQueue Audio Buffer TS being sent to audio sink %lld buffer size %d",mediaTimeUs,copy);
        CHECK_EQ(mAudioSink->write(entry->mBuffer->data() + entry->mOffset, copy),(ssize_t)copy);
        entry->mOffset += copy;
        mLastSentAudioSinkTSUs = mediaTimeUs;
        ALOGV("onDrainAudioQueue copy %d updated offset %d",copy,entry->mOffset);
        if (entry->mOffset == entry->mBuffer->size()) {
			entry->mNotifyConsumed->post();
			mAudioQueue.erase(mAudioQueue.begin());
			entry = NULL;
		} else {
			ALOGE("onDrainAudioQueue, please check as could not write size worth of data to audio sink...");
		}
#endif

    notifyPosition();

    return !mAudioQueue.empty();
}

void NuPlayer::HLSRenderer::postDrainVideoQueue() {
	ALOGV("HLSRenderer::postDrainVideoQueue");
    if (mDrainVideoQueuePending || mSyncQueues || mPaused) {
		ALOGV("HLSRenderer::postDrainVideoQueue returning from (mDrainVideoQueuePending || mSyncQueues || mPaused) check");
        return;
    }

    if (mVideoQueue.empty()) {
		ALOGV("HLSRenderer::postDrainVideoQueue returning from mVideoQueue.empty() check");
        return;
    }

    QueueEntry &entry = *mVideoQueue.begin();

    sp<AMessage> msg = new AMessage(kWhatDrainVideoQueue, id());
    msg->setInt32("generation", mVideoQueueGeneration);

    int64_t delayUs;

    if (entry.mBuffer == NULL) {
        // EOS doesn't carry a timestamp.
        delayUs = 0;
        ALOGV("HLSRenderer::postDrainVideoQueue video EOS");
    } else {
        int64_t mediaTimeUs;
        CHECK(entry.mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
        ALOGV("HLSRenderer::postDrainVideoQueue video mediaTimeUs %lld mAnchorTimeMediaUs %lld",mediaTimeUs,mAnchorTimeMediaUs);

        if (mAnchorTimeMediaUs < 0) {
            delayUs = 0;

            if (!mHasAudio) {
                mAnchorTimeMediaUs = mediaTimeUs;
                mAnchorTimeRealUs = ALooper::GetNowUs();
                ALOGV("HLSRenderer::postDrainVideoQueue set mAnchorTimeMediaUs %lld mAnchorTimeRealUs %lld",
			mAnchorTimeMediaUs,mAnchorTimeRealUs);
            }
        } else {
			ALOGV("HLSRenderer::postDrainVideoQueue calculating realTimeUs = (mediaTimeUs(%lld)- mAnchorTimeMediaUs(%lld)) + mAnchorTimeRealUs(%lld)",
					mediaTimeUs,mAnchorTimeMediaUs,mAnchorTimeRealUs);

            int64_t realTimeUs =
                (mediaTimeUs - mAnchorTimeMediaUs) + mAnchorTimeRealUs;
            ALOGV("HLSRenderer::postDrainVideoQueue realTimeUs %lld",realTimeUs);
            int64_t clock = ALooper::GetNowUs();
            ALOGV("HLSRenderer::postDrainVideoQueue clock %lld",clock);
            delayUs = realTimeUs - clock;
            ALOGV("HLSRenderer::postDrainVideoQueue delayUs(realTimeUs - clock) %lld",delayUs);
        }
    }
    ALOGV("HLSRenderer::postDrainVideoQueue posting kWhatDrainVideoQueue");

    msg->post(delayUs);

    mDrainVideoQueuePending = true;
}

void NuPlayer::HLSRenderer::onDrainVideoQueue() {
	ALOGV("HLSRenderer::onDrainVideoQueue");
    if (mVideoQueue.empty()) {
		ALOGV("HLSRenderer::onDrainVideoQueue mVideoQueue empty");
        return;
    }

    QueueEntry *entry = &*mVideoQueue.begin();

    if (entry->mBuffer == NULL) {
        // EOS

        notifyEOS(false /* audio */, entry->mFinalResult);

        mVideoQueue.erase(mVideoQueue.begin());
        entry = NULL;

        mVideoLateByUs = 0ll;
        ALOGV("HLSRenderer::onDrainVideoQueue video EOS..");

        notifyPosition();
        return;
    }

    int64_t mediaTimeUs;
    CHECK(entry->mBuffer->meta()->findInt64("timeUs", &mediaTimeUs));
    ALOGV("HLSRenderer::onDrainVideoQueue video mediaTimeUs %lld mAnchorTimeMediaUs %lld mAnchorTimeRealUs %lld",mediaTimeUs,mAnchorTimeMediaUs,mAnchorTimeRealUs);

    int64_t realTimeUs = mediaTimeUs - mAnchorTimeMediaUs + mAnchorTimeRealUs;
    ALOGV("HLSRenderer::onDrainVideoQueue realTimeUs %lld",realTimeUs);
    int64_t clock = ALooper::GetNowUs();
    ALOGV("HLSRenderer::onDrainVideoQueue clock %lld",clock);

    mVideoLateByUs =  clock - realTimeUs;
    ALOGV("HLSRenderer::onDrainVideoQueue mVideoLateByUs(clock - realTimeUs) %lld",mVideoLateByUs);

    bool tooLate = (mVideoLateByUs > TUNNEL_RENDERER_AV_SYNC_WINDOW);

    if (tooLate) {
        ALOGV("video mediaTimeUs %lld late by %lld msec",mediaTimeUs,mVideoLateByUs/1000);
    } else {
        ALOGV("rendering video at media time mediaTimeUs %lld ", mediaTimeUs);
    }

    entry->mNotifyConsumed->setInt32("render", !tooLate);
    entry->mNotifyConsumed->post();
    mVideoQueue.erase(mVideoQueue.begin());
    entry = NULL;

    if (!mVideoRenderingStarted) {
        mVideoRenderingStarted = true;
        notifyVideoRenderingStart();
    }

    notifyPosition();
}

void NuPlayer::HLSRenderer::notifyVideoRenderingStart() {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatVideoRenderingStart);
    ALOGV("HLSRenderer::notifyVideoRenderingStart");
    notify->post();
}

void NuPlayer::HLSRenderer::notifyEOS(bool audio, status_t finalResult) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatEOS);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    notify->setInt32("finalResult", finalResult);
    ALOGV("HLSRenderer::notifyEOS audio? %d",audio);
    notify->post();
}

void NuPlayer::HLSRenderer::onQueueBuffer(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (audio) {
        mHasAudio = true;
    } else {
        mHasVideo = true;
    }

    if (dropBufferWhileFlushing(audio, msg)) {
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
    ALOGV("HLSRenderer::onQueueBuffer");

    if (audio) {
        mAudioQueue.push_back(entry);
        ALOGV("HLSRenderer::onQueueBuffer calling postDrainAudioQueue");
        postDrainAudioQueue();
    } else {
        mVideoQueue.push_back(entry);
        ALOGV("HLSRenderer::onQueueBuffer calling postDrainVideoQueue");
        postDrainVideoQueue();
    }

    if (!mSyncQueues || mAudioQueue.empty() || mVideoQueue.empty()) {
        return;
    }

    sp<ABuffer> firstAudioBuffer = (*mAudioQueue.begin()).mBuffer;
    sp<ABuffer> firstVideoBuffer = (*mVideoQueue.begin()).mBuffer;

    if (firstAudioBuffer == NULL || firstVideoBuffer == NULL) {
        // EOS signalled on either queue.
        syncQueuesDone();
        return;
    }

    int64_t firstAudioTimeUs;
    int64_t firstVideoTimeUs;
    CHECK(firstAudioBuffer->meta()
            ->findInt64("timeUs", &firstAudioTimeUs));
    CHECK(firstVideoBuffer->meta()
            ->findInt64("timeUs", &firstVideoTimeUs));

    int64_t diff = firstVideoTimeUs - firstAudioTimeUs;
    ALOGV("HLSRenderer::onQueueBuffer diff %lld = firstVideoTimeUs %lld - firstAudioTimeUs %lld",
		diff,firstVideoTimeUs,firstAudioTimeUs);

    ALOGV("queueDiff = %lld msec", diff / 1000);

    if (diff > 100000ll) {
        // Audio data starts More than 0.1 secs before video.
        // Drop some audio.

        (*mAudioQueue.begin()).mNotifyConsumed->post();
        mAudioQueue.erase(mAudioQueue.begin());
        ALOGV("dropping some audio as Audio data starts More than 0.1 secs before video");
        return;
    }

    syncQueuesDone();
}

void NuPlayer::HLSRenderer::syncQueuesDone() {
    if (!mSyncQueues) {
        return;
    }

    mSyncQueues = false;

    if (!mAudioQueue.empty()) {
		ALOGV("syncQueuesDone calling postDrainAudioQueue");
        postDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
		ALOGV("syncQueuesDone calling postDrainVideoQueue");
        postDrainVideoQueue();
    }
}

void NuPlayer::HLSRenderer::onQueueEOS(const sp<AMessage> &msg) {
    int32_t audio;
    CHECK(msg->findInt32("audio", &audio));

    if (dropBufferWhileFlushing(audio, msg)) {
        return;
    }

    int32_t finalResult;
    CHECK(msg->findInt32("finalResult", &finalResult));

    QueueEntry entry;
    entry.mOffset = 0;
    entry.mFinalResult = finalResult;

    if (audio) {
        mAudioQueue.push_back(entry);
        ALOGV("onQueueEOS audio calling postDrainAudioQueue");
        postDrainAudioQueue();
    } else {
        mVideoQueue.push_back(entry);
        ALOGV("onQueueEOS video calling postDrainVideoQueue");
        postDrainVideoQueue();
    }
}

void NuPlayer::HLSRenderer::onFlush(const sp<AMessage> &msg) {
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
    syncQueuesDone();
    ALOGV("HLSRenderer::onFlush..");

    if (audio) {
		ALOGV("HLSRenderer::onFlush audio");
        flushQueue(&mAudioQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingAudio = false;

        mDrainAudioQueuePending = false;
        ++mAudioQueueGeneration;
    } else {
		ALOGV("HLSRenderer::onFlush video");
        flushQueue(&mVideoQueue);

        Mutex::Autolock autoLock(mFlushLock);
        mFlushingVideo = false;

        mDrainVideoQueuePending = false;
        ++mVideoQueueGeneration;
    }

    notifyFlushComplete(audio);
}

void NuPlayer::HLSRenderer::flushQueue(List<QueueEntry> *queue) {
	ALOGV("HLSRenderer::flushQueue");
    while (!queue->empty()) {
        QueueEntry *entry = &*queue->begin();

        if (entry->mBuffer != NULL) {
            entry->mNotifyConsumed->post();
        }

        queue->erase(queue->begin());
        entry = NULL;
    }
}

void NuPlayer::HLSRenderer::notifyFlushComplete(bool audio) {
    sp<AMessage> notify = mNotify->dup();
    notify->setInt32("what", kWhatFlushComplete);
    notify->setInt32("audio", static_cast<int32_t>(audio));
    ALOGV("HLSRenderer::notifyFlushComplete audio? %d",audio);
    ALOGV("HLSRenderer::notifyFlushComplete audio queue has %d entries, video has %d entries",
			mAudioQueue.size(), mVideoQueue.size());
    notify->post();
}

bool NuPlayer::HLSRenderer::dropBufferWhileFlushing(
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
    ALOGV("HLSRenderer::dropBufferWhileFlushing audio? %d",audio);

    sp<AMessage> notifyConsumed;
    if (msg->findMessage("notifyConsumed", &notifyConsumed)) {
        notifyConsumed->post();
    }

    return true;
}

void NuPlayer::HLSRenderer::onAudioSinkChanged() {
    CHECK(!mDrainAudioQueuePending);
    mNumFramesWritten = 0;
    ALOGV("HLSRenderer::onAudioSinkChanged: TODO");
#if 0
    uint32_t written;
    if (mAudioSink->getFramesWritten(&written) == OK) {
        mNumFramesWritten = written;
    }
#endif
}

void NuPlayer::HLSRenderer::notifyPosition() {
	ALOGV("HLSRenderer::notifyPosition");
    if (mAnchorTimeRealUs < 0 || mAnchorTimeMediaUs < 0) {
		ALOGV("HLSRenderer::notifyPosition anchor times not set...returning");
        return;
    }

    int64_t nowUs = ALooper::GetNowUs();

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
    ALOGV("HLSRenderer::notifyPosition positionUs %lld mVideoLateByUs %lld",positionUs,mVideoLateByUs);
    notify->post();
}

void NuPlayer::HLSRenderer::onPause() {
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
}

void NuPlayer::HLSRenderer::onResume() {
	ALOGV("HLSRenderer::onResume");
    if (!mPaused) {
        return;
    }

    if (mHasAudio) {
        mAudioSink->start();
    }

    mPaused = false;

    if (!mAudioQueue.empty()) {
		ALOGV("HLSRenderer::onResume calling postDrainAudioQueue");
        postDrainAudioQueue();
    }

    if (!mVideoQueue.empty()) {
		ALOGV("HLSRenderer::onResume calling postDrainVideoQueue");
        postDrainVideoQueue();
    }
}

status_t NuPlayer::HLSRenderer::setMediaPresence(bool audio, bool bValue)
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
}  // namespace android
