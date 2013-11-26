/*
**
** Copyright (c) 2013, The Linux Foundation. All rights reserved.
** Not a Contribution
**
** Copyright 2010, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**	   http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "IResourceManagerDeathNotifier"
#include <utils/Log.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>
#include <IResourceManagerDeathNotifier.h>

namespace android {

// client singleton for binder interface to ResourceManager Service
Mutex IResourceManagerDeathNotifier::sServiceLock;
sp<IResourceManagerService> IResourceManagerDeathNotifier::sResourceManagerService;
sp<IResourceManagerDeathNotifier::DeathNotifier> IResourceManagerDeathNotifier::sDeathNotifier;
SortedVector< wp<IResourceManagerDeathNotifier> > IResourceManagerDeathNotifier::sObitRecipients;

// establish binder interface to ResourceManagerService
const sp<IResourceManagerService>& IResourceManagerDeathNotifier::getResourceManagerService()
{
    ALOGV("getResourceManagerService");
    Mutex::Autolock _l(sServiceLock);
    if (sResourceManagerService == 0) {
        sp<IServiceManager> sm = defaultServiceManager();
        sp<IBinder> binder;
        do {
            binder = sm->getService(String16("resourcemanager.service"));
            if (binder != 0) {
                break;
            }
            ALOGV("ResourceManager service not published, waiting...");
            usleep(500000); // 0.5 s
        } while (true);

        if (sDeathNotifier == NULL) {
           sDeathNotifier = new DeathNotifier();
        }
        binder->linkToDeath(sDeathNotifier);
        sResourceManagerService = interface_cast<IResourceManagerService>(binder);
    }
    ALOGE_IF(sResourceManagerService == 0, "no ResourceManager service!?");
    return sResourceManagerService;
}

void IResourceManagerDeathNotifier::addObitRecipient(const wp<IResourceManagerDeathNotifier>& recipient)
{
    ALOGV("addObitRecipient");
    Mutex::Autolock _l(sServiceLock);
    sObitRecipients.add(recipient);
}

void IResourceManagerDeathNotifier::removeObitRecipient(const wp<IResourceManagerDeathNotifier>& recipient)
{
    ALOGV("removeObitRecipient");
    Mutex::Autolock _l(sServiceLock);
    sObitRecipients.remove(recipient);
}

void IResourceManagerDeathNotifier::DeathNotifier::binderDied(const wp<IBinder>& who)
{
    ALOGV("ResourceManager server died");

    // Need to do this with the lock held
    SortedVector< wp<IResourceManagerDeathNotifier> > list;
    {
        Mutex::Autolock _l(sServiceLock);
        sResourceManagerService.clear();
        list = sObitRecipients;
    }

    // Notify application when ResourceManager server dies.
    // Don't hold the static lock during callback in case app
    // makes a call that needs the lock.
    size_t count = list.size();
    for (size_t iter = 0; iter < count; ++iter) {
        sp<IResourceManagerDeathNotifier> notifier = list[iter].promote();
        if (notifier != 0) {
            notifier->died();
        }
    }
}

IResourceManagerDeathNotifier::DeathNotifier::~DeathNotifier()
{
    ALOGV("DeathNotifier::~DeathNotifier");
    Mutex::Autolock _l(sServiceLock);
    sObitRecipients.clear();
    if (sResourceManagerService != 0) {
        sResourceManagerService->asBinder()->unlinkToDeath(this);
    }
}

}; // namespace android
