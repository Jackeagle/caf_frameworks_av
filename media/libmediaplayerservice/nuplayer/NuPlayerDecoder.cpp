/*
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
#define LOG_TAG "NuPlayerDecoder"
#include <utils/Log.h>

#include "NuPlayerDecoder.h"

#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/ACodec.h>
#include <media/stagefright/MediaDefs.h>
#ifdef QCOM_WFD_SINK
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#endif //QCOM_WFD_SINK

namespace android {

NuPlayer::Decoder::Decoder(
        const sp<AMessage> &notify,
        const sp<NativeWindowWrapper> &nativeWindow)
    : mNotify(notify),
      mNativeWindow(nativeWindow) {
#ifdef QCOM_WFD_SINK
    IS_TARGET_MPQ(mIsTargetMPQ);
    mCreateMPQAudioHALwrapper = false;
    mMPQWrapper = NULL;
    mAudioSink = NULL;
#endif //QCOM_WFD_SINK
}

NuPlayer::Decoder::~Decoder() {
}

void NuPlayer::Decoder::configure(const sp<AMessage> &format, bool wfdSink) {
    CHECK(mCodec == NULL);

    AString mime;
    CHECK(format->findString("mime", &mime));

    sp<AMessage> notifyMsg =
        new AMessage(kWhatCodecNotify, id());

    mCSDIndex = 0;
    for (size_t i = 0;; ++i) {
        sp<ABuffer> csd;
        if (!format->findBuffer(StringPrintf("csd-%d", i).c_str(), &csd)) {
            break;
        }

        mCSD.push(csd);
    }

    if (mNativeWindow != NULL) {
        format->setObject("native-window", mNativeWindow);
    }

    // Current video decoders do not return from OMX_FillThisBuffer
    // quickly, violating the OpenMAX specs, until that is remedied
    // we need to invest in an extra looper to free the main event
    // queue.
    bool needDedicatedLooper = !strncasecmp(mime.c_str(), "video/", 6);

    mCodec = new ACodec;

#ifdef QCOM_WFD_SINK
    if (!needDedicatedLooper && wfdSink) //Audio for Wfd sink
    {
        // For wfd Audio we use the MPQ Hal Wrapper
        //free the ACodec allocated above.
        mCodec = NULL;
        mCreateMPQAudioHALwrapper = true;
        mMPQWrapper = new MPQHALWrapper(mAudioSink,mRenderer);
     }
#endif //QCOM_WFD_SINK

     if (needDedicatedLooper && mCodecLooper == NULL) {
        mCodecLooper = new ALooper;
        mCodecLooper->setName("NuPlayerDecoder");
        mCodecLooper->start(false, false, ANDROID_PRIORITY_AUDIO);
     }

#ifdef QCOM_WFD_SINK
    if(mCreateMPQAudioHALwrapper) {
         looper()->registerHandler(mMPQWrapper);
         mMPQWrapper->setNotificationMessage(notifyMsg);
         mMPQWrapper->initiateSetup(format);
     } else
#endif //QCOM_WFD_SINK
     {
         (needDedicatedLooper ? mCodecLooper : looper())->registerHandler(mCodec);
         mCodec->setNotificationMessage(notifyMsg);
         mCodec->initiateSetup(format);
     }
}

void NuPlayer::Decoder::onMessageReceived(const sp<AMessage> &msg) {
    switch (msg->what()) {
        case kWhatCodecNotify:
        {
            int32_t what;
            CHECK(msg->findInt32("what", &what));

            if (what == ACodec::kWhatFillThisBuffer) {
                onFillThisBuffer(msg);
            } else {
                sp<AMessage> notify = mNotify->dup();
                notify->setMessage("codec-request", msg);
                notify->post();
            }
            break;
        }

        default:
            TRESPASS();
            break;
    }
}

void NuPlayer::Decoder::onFillThisBuffer(const sp<AMessage> &msg) {
    sp<AMessage> reply;
    CHECK(msg->findMessage("reply", &reply));

#if 0
    sp<ABuffer> outBuffer;
    CHECK(msg->findBuffer("buffer", &outBuffer));
#else
    sp<ABuffer> outBuffer;
#endif

    if (mCSDIndex < mCSD.size()) {
        outBuffer = mCSD.editItemAt(mCSDIndex++);
        outBuffer->meta()->setInt64("timeUs", 0);

        reply->setBuffer("buffer", outBuffer);
        reply->post();
        return;
    }

    sp<AMessage> notify = mNotify->dup();
    notify->setMessage("codec-request", msg);
    notify->post();
}

void NuPlayer::Decoder::signalFlush() {
#ifdef QCOM_WFD_SINK
    if(mCreateMPQAudioHALwrapper) {
        if (mMPQWrapper!= NULL) {
            mMPQWrapper->signalFlush();
        }
    } else
#endif //QCOM_WFD_SINK
    {
        if (mCodec != NULL) {
           mCodec->signalFlush();
        }
    }
}

void NuPlayer::Decoder::signalResume() {
#ifdef QCOM_WFD_SINK
    if(mCreateMPQAudioHALwrapper) {
        if (mMPQWrapper!= NULL) {
            mMPQWrapper->signalResume();
        }
    } else
#endif //QCOM_WFD_SINK
    {
        if (mCodec != NULL) {
           mCodec->signalResume();
        }
    }
}

void NuPlayer::Decoder::initiateShutdown() {
#ifdef QCOM_WFD_SINK
    if(mCreateMPQAudioHALwrapper) {
        if (mMPQWrapper!= NULL) {
            mMPQWrapper->initiateShutdown();
        }
    } else
#endif //QCOM_WFD_SINK
    {
        if (mCodec != NULL) {
           mCodec->initiateShutdown();
        }
    }
}

void NuPlayer::Decoder::setSink(const sp<MediaPlayerBase::AudioSink> &sink, sp<Renderer> Renderer) {
    mAudioSink = sink;
    mRenderer  = Renderer;
}

}  // namespace android

