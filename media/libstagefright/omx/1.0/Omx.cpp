/*
 * Copyright 2016, The Android Open Source Project
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

#include <ios>
#include <list>

#include <android-base/logging.h>
#include <gui/IGraphicBufferProducer.h>
#include <OMX_Core.h>
#include <OMX_AsString.h>

#include "../OMXUtils.h"
#include "../OMXMaster.h"
#include "../GraphicBufferSource.h"

#include "WOmxNode.h"
#include "WOmxObserver.h"
#include "WGraphicBufferProducer.h"
#include "WGraphicBufferSource.h"
#include "Conversion.h"

#include "Omx.h"

namespace android {
namespace hardware {
namespace media {
namespace omx {
namespace V1_0 {
namespace implementation {

constexpr size_t kMaxNodeInstances = (1 << 16);

Omx::Omx() :
    mMaster(new OMXMaster()),
    mParser() {
}

Omx::~Omx() {
    delete mMaster;
}

Return<void> Omx::listNodes(listNodes_cb _hidl_cb) {
    std::list<::android::IOMX::ComponentInfo> list;
    char componentName[256];
    for (OMX_U32 index = 0;
            mMaster->enumerateComponents(
            componentName, sizeof(componentName), index) == OMX_ErrorNone;
            ++index) {
        list.push_back(::android::IOMX::ComponentInfo());
        ::android::IOMX::ComponentInfo& info = list.back();
        info.mName = componentName;
        ::android::Vector<::android::String8> roles;
        OMX_ERRORTYPE err =
                mMaster->getRolesOfComponent(componentName, &roles);
        if (err == OMX_ErrorNone) {
            for (OMX_U32 i = 0; i < roles.size(); ++i) {
                info.mRoles.push_back(roles[i]);
            }
        }
    }

    hidl_vec<ComponentInfo> tList;
    tList.resize(list.size());
    size_t i = 0;
    for (auto const& info : list) {
        convertTo(&(tList[i++]), info);
    }
    _hidl_cb(toStatus(OK), tList);
    return Void();
}

Return<void> Omx::allocateNode(
        const hidl_string& name,
        const sp<IOmxObserver>& observer,
        allocateNode_cb _hidl_cb) {

    using ::android::IOMXNode;
    using ::android::IOMXObserver;

    Mutex::Autolock autoLock(mLock);
    if (mLiveNodes.size() == kMaxNodeInstances) {
        _hidl_cb(toStatus(NO_MEMORY), nullptr);
        return Void();
    }

    sp<OMXNodeInstance> instance = new OMXNodeInstance(
            this, new LWOmxObserver(observer), name.c_str());

    OMX_COMPONENTTYPE *handle;
    OMX_ERRORTYPE err = mMaster->makeComponentInstance(
            name.c_str(), &OMXNodeInstance::kCallbacks,
            instance.get(), &handle);

    if (err != OMX_ErrorNone) {
        LOG(ERROR) << "Failed to allocate omx component "
                "'" << name.c_str() << "' "
                " err=" << asString(err) <<
                "(0x" << std::hex << unsigned(err) << ")";
        _hidl_cb(toStatus(StatusFromOMXError(err)), nullptr);
        return Void();
    }
    instance->setHandle(handle);
    std::vector<AString> quirkVector;
    if (mParser.getQuirks(name.c_str(), &quirkVector) == OK) {
        uint32_t quirks = 0;
        for (const AString quirk : quirkVector) {
            if (quirk == "requires-allocate-on-input-ports") {
                quirks |= kRequiresAllocateBufferOnInputPorts;
            }
            if (quirk == "requires-allocate-on-output-ports") {
                quirks |= kRequiresAllocateBufferOnOutputPorts;
            }
        }
        instance->setQuirks(quirks);
    }

    mLiveNodes.add(observer.get(), instance);
    observer->linkToDeath(this, 0);
    mNode2Observer.add(instance.get(), observer.get());

    _hidl_cb(toStatus(OK), new TWOmxNode(instance));
    return Void();
}

Return<void> Omx::createInputSurface(createInputSurface_cb _hidl_cb) {
    sp<::android::IGraphicBufferProducer> bufferProducer;

    sp<GraphicBufferSource> graphicBufferSource = new GraphicBufferSource();
    status_t err = graphicBufferSource->initCheck();
    if (err != OK) {
        LOG(ERROR) << "Failed to create persistent input surface: "
                << strerror(-err) << " "
                "(" << int(err) << ")";
        _hidl_cb(toStatus(err), nullptr, nullptr);
        return Void();
    }
    bufferProducer = graphicBufferSource->getIGraphicBufferProducer();

    _hidl_cb(toStatus(OK),
            new TWGraphicBufferProducer(bufferProducer),
            new TWGraphicBufferSource(graphicBufferSource));
    return Void();
}

void Omx::serviceDied(uint64_t /* cookie */, wp<IBase> const& who) {
    sp<OMXNodeInstance> instance;
    {
        Mutex::Autolock autoLock(mLock);

        ssize_t index = mLiveNodes.indexOfKey(who);

        if (index < 0) {
            LOG(ERROR) << "b/27597103, nonexistent observer on serviceDied";
            android_errorWriteLog(0x534e4554, "27597103");
            return;
        }

        instance = mLiveNodes.editValueAt(index);
        mLiveNodes.removeItemsAt(index);
        mNode2Observer.removeItem(instance.get());
    }
    instance->onObserverDied();
}

status_t Omx::freeNode(sp<OMXNodeInstance> const& instance) {
    if (instance == NULL) {
        return OK;
    }

    {
        Mutex::Autolock autoLock(mLock);
        ssize_t observerIndex = mNode2Observer.indexOfKey(instance.get());
        if (observerIndex >= 0) {
            wp<IBase> observer = mNode2Observer.valueAt(observerIndex);
            ssize_t nodeIndex = mLiveNodes.indexOfKey(observer);
            if (nodeIndex >= 0) {
                mNode2Observer.removeItemsAt(observerIndex);
                mLiveNodes.removeItemsAt(nodeIndex);
                sp<IBase> sObserver = observer.promote();
                if (sObserver != nullptr) {
                    sObserver->unlinkToDeath(this);
                }
            } else {
                LOG(WARNING) << "Inconsistent observer record";
            }
        }
    }

    OMX_ERRORTYPE err = OMX_ErrorNone;
    if (instance->handle() != NULL) {
        err = mMaster->destroyComponentInstance(
                static_cast<OMX_COMPONENTTYPE*>(instance->handle()));
    }
    return StatusFromOMXError(err);
}

// Methods from ::android::hidl::base::V1_0::IBase follow.

IOmx* HIDL_FETCH_IOmx(const char* /* name */) {
    return new Omx();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace omx
}  // namespace media
}  // namespace hardware
}  // namespace android
