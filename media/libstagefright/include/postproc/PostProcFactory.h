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

#ifndef POSTPROC_FACTORY_H_
#define POSTPROC_FACTORY_H_

#include <media/stagefright/MediaSource.h>
#include <media/IOMX.h>
#include <postproc/PostProcNativeWindow.h>
#include <postproc/PostProcController.h>

namespace android {

enum PostProcType {
    PostProcType2DTo3D
};

sp<MediaSource> PostProcFactoryCreate(const sp<MediaSource> &source, const sp<PostProcNativeWindow> &nativeWindow, const sp<MetaData> &meta, PostProcController **ppController, PostProcType ppType);
}

#endif  // POSTPROC_FACTORY_H_
