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

#ifndef ANDROID_IRESOURCEMANAGERDEATHNOTIFIER_H
#define ANDROID_IRESOURCEMANAGERDEATHNOTIFIER_H

#include <utils/threads.h>
#include <utils/SortedVector.h>
#include <IResourceManagerService.h>

namespace android {

class IResourceManagerDeathNotifier: virtual public RefBase
{
public:
    IResourceManagerDeathNotifier() { addObitRecipient(this); }
    virtual ~IResourceManagerDeathNotifier() { removeObitRecipient(this); }

    virtual void died() = 0;
    static const sp<IResourceManagerService>& getResourceManagerService();

private:
    IResourceManagerDeathNotifier &operator=(const IResourceManagerDeathNotifier &);
    IResourceManagerDeathNotifier(const IResourceManagerDeathNotifier &);

    static void addObitRecipient(const wp<IResourceManagerDeathNotifier>& recipient);
    static void removeObitRecipient(const wp<IResourceManagerDeathNotifier>& recipient);

    class DeathNotifier: public IBinder::DeathRecipient
    {
    public:
        DeathNotifier() {}
        virtual ~DeathNotifier();
        virtual void binderDied(const wp<IBinder>& who);
    };

    friend class DeathNotifier;

    static  Mutex                                    sServiceLock;
    static  sp<IResourceManagerService>              sResourceManagerService;
    static  sp<DeathNotifier>                        sDeathNotifier;
    static  SortedVector< wp<IResourceManagerDeathNotifier> > sObitRecipients;
};

}; // namespace android

#endif // ANDROID_IRESOURCEMANAGERDEATHNOTIFIER_H
