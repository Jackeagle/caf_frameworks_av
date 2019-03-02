/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "APM_AudioPolicyMix"
//#define LOG_NDEBUG 0

#include "AudioPolicyMix.h"
#include "TypeConverter.h"
#include "HwModule.h"
#include "AudioPort.h"
#include "IOProfile.h"
#include "AudioGain.h"
#include <AudioOutputDescriptor.h>

namespace android {

void AudioPolicyMix::setOutput(sp<SwAudioOutputDescriptor> &output)
{
    mOutput = output;
}

const sp<SwAudioOutputDescriptor> &AudioPolicyMix::getOutput() const
{
    return mOutput;
}

void AudioPolicyMix::clearOutput()
{
    mOutput.clear();
}

void AudioPolicyMix::setMix(AudioMix &mix)
{
    mMix = mix;
}

android::AudioMix *AudioPolicyMix::getMix()
{
    return &mMix;
}

void AudioPolicyMix::dump(String8 *dst, int spaces, int index) const
{
    dst->appendFormat("%*sAudio Policy Mix %d:\n", spaces, "", index + 1);
    std::string mixTypeLiteral;
    if (!MixTypeConverter::toString(mMix.mMixType, mixTypeLiteral)) {
        ALOGE("%s: failed to convert mix type %d", __FUNCTION__, mMix.mMixType);
        return;
    }
    dst->appendFormat("%*s- mix type: %s\n", spaces, "", mixTypeLiteral.c_str());

    std::string routeFlagLiteral;
    RouteFlagTypeConverter::maskToString(mMix.mRouteFlags, routeFlagLiteral);
    dst->appendFormat("%*s- Route Flags: %s\n", spaces, "", routeFlagLiteral.c_str());

    std::string deviceLiteral;
    deviceToString(mMix.mDeviceType, deviceLiteral);
    dst->appendFormat("%*s- device type: %s\n", spaces, "", deviceLiteral.c_str());

    dst->appendFormat("%*s- device address: %s\n", spaces, "", mMix.mDeviceAddress.string());

    int indexCriterion = 0;
    for (const auto &criterion : mMix.mCriteria) {
        dst->appendFormat("%*s- Criterion %d:\n", spaces + 2, "", indexCriterion++);

        std::string usageLiteral;
        if (!UsageTypeConverter::toString(criterion.mValue.mUsage, usageLiteral)) {
            ALOGE("%s: failed to convert usage %d", __FUNCTION__, criterion.mValue.mUsage);
            return;
        }
        dst->appendFormat("%*s- Usage:%s\n", spaces + 4, "", usageLiteral.c_str());

        if (mMix.mMixType == MIX_TYPE_RECORDERS) {
            std::string sourceLiteral;
            if (!SourceTypeConverter::toString(criterion.mValue.mSource, sourceLiteral)) {
                ALOGE("%s: failed to convert source %d", __FUNCTION__, criterion.mValue.mSource);
                return;
            }
            dst->appendFormat("%*s- Source:%s\n", spaces + 4, "", sourceLiteral.c_str());

        }
        dst->appendFormat("%*s- Uid:%d\n", spaces + 4, "", criterion.mValue.mUid);

        std::string ruleLiteral;
        if (!RuleTypeConverter::toString(criterion.mRule, ruleLiteral)) {
            ALOGE("%s: failed to convert source %d", __FUNCTION__,criterion.mRule);
            return;
        }
        dst->appendFormat("%*s- Rule:%s\n", spaces + 4, "", ruleLiteral.c_str());
    }
}

status_t AudioPolicyMixCollection::registerMix(const String8& address, AudioMix mix,
                                               sp<SwAudioOutputDescriptor> desc)
{
    ssize_t index = indexOfKey(address);
    if (index >= 0) {
        ALOGE("registerPolicyMixes(): mix for address %s already registered", address.string());
        return BAD_VALUE;
    }
    sp<AudioPolicyMix> policyMix = new AudioPolicyMix();
    policyMix->setMix(mix);
    add(address, policyMix);

    if (desc != 0) {
        desc->mPolicyMix = policyMix->getMix();
        policyMix->setOutput(desc);
    }
    return NO_ERROR;
}

status_t AudioPolicyMixCollection::unregisterMix(const String8& address)
{
    ssize_t index = indexOfKey(address);
    if (index < 0) {
        ALOGE("unregisterPolicyMixes(): mix for address %s not registered", address.string());
        return BAD_VALUE;
    }

    removeItemsAt(index);
    return NO_ERROR;
}

status_t AudioPolicyMixCollection::getAudioPolicyMix(const String8& address,
                                                     sp<AudioPolicyMix> &policyMix) const
{
    ssize_t index = indexOfKey(address);
    if (index < 0) {
        ALOGE("unregisterPolicyMixes(): mix for address %s not registered", address.string());
        return BAD_VALUE;
    }
    policyMix = valueAt(index);
    return NO_ERROR;
}

void AudioPolicyMixCollection::closeOutput(sp<SwAudioOutputDescriptor> &desc)
{
    for (size_t i = 0; i < size(); i++) {
        sp<AudioPolicyMix> policyMix = valueAt(i);
        if (policyMix->getOutput() == desc) {
            policyMix->clearOutput();
        }
    }
}

status_t AudioPolicyMixCollection::getOutputForAttr(audio_attributes_t attributes, uid_t uid,
                                                    sp<SwAudioOutputDescriptor> &desc)
{
    ALOGV("getOutputForAttr() querying %zu mixes:", size());
    desc = 0;
    for (size_t i = 0; i < size(); i++) {
        sp<AudioPolicyMix> policyMix = valueAt(i);
        AudioMix *mix = policyMix->getMix();

        if (mix->mMixType == MIX_TYPE_PLAYERS) {
            // TODO if adding more player rules (currently only 2), make rule handling "generic"
            //      as there is no difference in the treatment of usage- or uid-based rules
            bool hasUsageMatchRules = false;
            bool hasUsageExcludeRules = false;
            bool usageMatchFound = false;
            bool usageExclusionFound = false;

            bool hasUidMatchRules = false;
            bool hasUidExcludeRules = false;
            bool uidMatchFound = false;
            bool uidExclusionFound = false;

            bool hasAddrMatch = false;

            // iterate over all mix criteria to list what rules this mix contains
            for (size_t j = 0; j < mix->mCriteria.size(); j++) {
                ALOGV(" getOutputForAttr: mix %zu: inspecting mix criteria %zu of %zu",
                        i, j, mix->mCriteria.size());

                // if there is an address match, prioritize that match
                if (strncmp(attributes.tags, "addr=", strlen("addr=")) == 0 &&
                        strncmp(attributes.tags + strlen("addr="),
                                mix->mDeviceAddress.string(),
                                AUDIO_ATTRIBUTES_TAGS_MAX_SIZE - strlen("addr=") - 1) == 0) {
                    hasAddrMatch = true;
                    break;
                }

                switch (mix->mCriteria[j].mRule) {
                case RULE_MATCH_ATTRIBUTE_USAGE:
                    ALOGV("\tmix has RULE_MATCH_ATTRIBUTE_USAGE for usage %d",
                                                mix->mCriteria[j].mValue.mUsage);
                    hasUsageMatchRules = true;
                    if (mix->mCriteria[j].mValue.mUsage == attributes.usage) {
                        // found one match against all allowed usages
                        usageMatchFound = true;
                    }
                    break;
                case RULE_EXCLUDE_ATTRIBUTE_USAGE:
                    ALOGV("\tmix has RULE_EXCLUDE_ATTRIBUTE_USAGE for usage %d",
                            mix->mCriteria[j].mValue.mUsage);
                    hasUsageExcludeRules = true;
                    if (mix->mCriteria[j].mValue.mUsage == attributes.usage) {
                        // found this usage is to be excluded
                        usageExclusionFound = true;
                    }
                    break;
                case RULE_MATCH_UID:
                    ALOGV("\tmix has RULE_MATCH_UID for uid %d", mix->mCriteria[j].mValue.mUid);
                    hasUidMatchRules = true;
                    if (mix->mCriteria[j].mValue.mUid == uid) {
                        // found one UID match against all allowed UIDs
                        uidMatchFound = true;
                    }
                    break;
                case RULE_EXCLUDE_UID:
                    ALOGV("\tmix has RULE_EXCLUDE_UID for uid %d", mix->mCriteria[j].mValue.mUid);
                    hasUidExcludeRules = true;
                    if (mix->mCriteria[j].mValue.mUid == uid) {
                        // found this UID is to be excluded
                        uidExclusionFound = true;
                    }
                    break;
                default:
                    break;
                }

                // consistency checks: for each "dimension" of rules (usage, uid...), we can
                // only have MATCH rules, or EXCLUDE rules in each dimension, not a combination
                if (hasUsageMatchRules && hasUsageExcludeRules) {
                    ALOGE("getOutputForAttr: invalid combination of RULE_MATCH_ATTRIBUTE_USAGE"
                            " and RULE_EXCLUDE_ATTRIBUTE_USAGE in mix %zu", i);
                    return BAD_VALUE;
                }
                if (hasUidMatchRules && hasUidExcludeRules) {
                    ALOGE("getOutputForAttr: invalid combination of RULE_MATCH_UID"
                            " and RULE_EXCLUDE_UID in mix %zu", i);
                    return BAD_VALUE;
                }

                if ((hasUsageExcludeRules && usageExclusionFound)
                        || (hasUidExcludeRules && uidExclusionFound)) {
                    break; // stop iterating on criteria because an exclusion was found (will fail)
                }

            }//iterate on mix criteria

            // determine if exiting on success (or implicit failure as desc is 0)
            if (hasAddrMatch ||
                    !((hasUsageExcludeRules && usageExclusionFound) ||
                      (hasUsageMatchRules && !usageMatchFound)  ||
                      (hasUidExcludeRules && uidExclusionFound) ||
                      (hasUidMatchRules && !uidMatchFound))) {
                ALOGV("\tgetOutputForAttr will use mix %zu", i);
                desc = policyMix->getOutput();
            }

        } else if (mix->mMixType == MIX_TYPE_RECORDERS) {
            if (attributes.usage == AUDIO_USAGE_VIRTUAL_SOURCE &&
                    strncmp(attributes.tags, "addr=", strlen("addr=")) == 0 &&
                    strncmp(attributes.tags + strlen("addr="),
                            mix->mDeviceAddress.string(),
                            AUDIO_ATTRIBUTES_TAGS_MAX_SIZE - strlen("addr=") - 1) == 0) {
                desc = policyMix->getOutput();
            }
        }
        if (desc != 0) {
            desc->mPolicyMix = mix;
            return NO_ERROR;
        }
    }
    return BAD_VALUE;
}

sp<DeviceDescriptor> AudioPolicyMixCollection::getDeviceAndMixForOutput(
        const sp<SwAudioOutputDescriptor> &output,
        const DeviceVector &availableOutputDevices,
        AudioMix **policyMix)
{
    for (size_t i = 0; i < size(); i++) {
        if (valueAt(i)->getOutput() == output) {
            AudioMix *mix = valueAt(i)->getMix();
            if (policyMix != nullptr)
                *policyMix = mix;
            // This Desc is involved in a Mix, which has the highest prio
            audio_devices_t deviceType = mix->mDeviceType;
            String8 address = mix->mDeviceAddress;
            ALOGV("%s: device (0x%x, addr=%s) forced by mix",
                  __FUNCTION__, deviceType, address.c_str());
            return availableOutputDevices.getDevice(deviceType, address, AUDIO_FORMAT_DEFAULT);
        }
    }
    return nullptr;
}

sp<DeviceDescriptor> AudioPolicyMixCollection::getDeviceAndMixForInputSource(
        audio_source_t inputSource, const DeviceVector &availDevices, AudioMix **policyMix) const
{
    for (size_t i = 0; i < size(); i++) {
        AudioMix *mix = valueAt(i)->getMix();
        if (mix->mMixType != MIX_TYPE_RECORDERS) {
            continue;
        }
        for (size_t j = 0; j < mix->mCriteria.size(); j++) {
            if ((RULE_MATCH_ATTRIBUTE_CAPTURE_PRESET == mix->mCriteria[j].mRule &&
                    mix->mCriteria[j].mValue.mSource == inputSource) ||
               (RULE_EXCLUDE_ATTRIBUTE_CAPTURE_PRESET == mix->mCriteria[j].mRule &&
                    mix->mCriteria[j].mValue.mSource != inputSource)) {
                // assuming PolicyMix only for remote submix for input
                // so mix->mDeviceType can only be AUDIO_DEVICE_OUT_REMOTE_SUBMIX
                audio_devices_t device = AUDIO_DEVICE_IN_REMOTE_SUBMIX;
                auto mixDevice =
                        availDevices.getDevice(device, mix->mDeviceAddress, AUDIO_FORMAT_DEFAULT);
                if (mixDevice != nullptr) {
                    if (policyMix != NULL) {
                        *policyMix = mix;
                    }
                    return mixDevice;
                }
                break;
            }
        }
    }
    return nullptr;
}

status_t AudioPolicyMixCollection::getInputMixForAttr(audio_attributes_t attr, AudioMix **policyMix)
{
    if (strncmp(attr.tags, "addr=", strlen("addr=")) != 0) {
        return BAD_VALUE;
    }
    String8 address(attr.tags + strlen("addr="));

#ifdef LOG_NDEBUG
    ALOGV("getInputMixForAttr looking for address %s\n  mixes available:", address.string());
    for (size_t i = 0; i < size(); i++) {
            sp<AudioPolicyMix> policyMix = valueAt(i);
            AudioMix *mix = policyMix->getMix();
            ALOGV("\tmix %zu address=%s", i, mix->mDeviceAddress.string());
    }
#endif

    ssize_t index = indexOfKey(address);
    if (index < 0) {
        ALOGW("getInputMixForAttr() no policy for address %s", address.string());
        return BAD_VALUE;
    }
    sp<AudioPolicyMix> audioPolicyMix = valueAt(index);
    AudioMix *mix = audioPolicyMix->getMix();

    if (mix->mMixType != MIX_TYPE_PLAYERS) {
        ALOGW("getInputMixForAttr() bad policy mix type for address %s", address.string());
        return BAD_VALUE;
    }
    *policyMix = mix;
    return NO_ERROR;
}

status_t AudioPolicyMixCollection::setUidDeviceAffinities(uid_t uid,
        const Vector<AudioDeviceTypeAddr>& devices) {
    // remove existing rules for this uid
    removeUidDeviceAffinities(uid);

    // for each player mix: add a rule to match or exclude the uid based on the device
    for (size_t i = 0; i < size(); i++) {
        const AudioMix *mix = valueAt(i)->getMix();
        if (mix->mMixType != MIX_TYPE_PLAYERS) {
            continue;
        }
        // check if this mix goes to a device in the list of devices
        bool deviceMatch = false;
        for (size_t j = 0; j < devices.size(); j++) {
            if (devices[j].mType == mix->mDeviceType
                    && devices[j].mAddress == mix->mDeviceAddress) {
                deviceMatch = true;
                break;
            }
        }
        if (deviceMatch) {
            mix->setMatchUid(uid);
        } else {
            // this mix doesn't go to one of the listed devices for the given uid,
            // modify its rules to exclude the uid
            mix->setExcludeUid(uid);
        }
    }

    return NO_ERROR;
}

status_t AudioPolicyMixCollection::removeUidDeviceAffinities(uid_t uid) {
    // for each player mix: remove existing rules that match or exclude this uid
    for (size_t i = 0; i < size(); i++) {
        bool foundUidRule = false;
        AudioMix *mix = valueAt(i)->getMix();
        if (mix->mMixType != MIX_TYPE_PLAYERS) {
            continue;
        }
        std::vector<size_t> criteriaToRemove;
        for (size_t j = 0; j < mix->mCriteria.size(); j++) {
            const uint32_t rule = mix->mCriteria[j].mRule;
            // is this rule affecting the uid?
            if ((rule == RULE_EXCLUDE_UID || rule == RULE_MATCH_UID)
                    && uid == mix->mCriteria[j].mValue.mUid) {
                foundUidRule = true;
                criteriaToRemove.insert(criteriaToRemove.begin(), j);
            }
        }
        if (foundUidRule) {
            for (size_t j = 0; j < criteriaToRemove.size(); j++) {
                mix->mCriteria.removeAt(criteriaToRemove[j]);
            }
        }
    }
    return NO_ERROR;
}

status_t AudioPolicyMixCollection::getDevicesForUid(uid_t uid,
        Vector<AudioDeviceTypeAddr>& devices) const {
    // for each player mix: find rules that don't exclude this uid, and add the device to the list
    for (size_t i = 0; i < size(); i++) {
        bool ruleAllowsUid = true;
        AudioMix *mix = valueAt(i)->getMix();
        if (mix->mMixType != MIX_TYPE_PLAYERS) {
            continue;
        }
        for (size_t j = 0; j < mix->mCriteria.size(); j++) {
            const uint32_t rule = mix->mCriteria[j].mRule;
            if (rule == RULE_EXCLUDE_UID
                    && uid == mix->mCriteria[j].mValue.mUid) {
                ruleAllowsUid = false;
                break;
            }
        }
        if (ruleAllowsUid) {
            devices.add(AudioDeviceTypeAddr(mix->mDeviceType, mix->mDeviceAddress));
        }
    }
    return NO_ERROR;
}

void AudioPolicyMixCollection::dump(String8 *dst) const
{
    dst->append("\nAudio Policy Mix:\n");
    for (size_t i = 0; i < size(); i++) {
        valueAt(i)->dump(dst, 2, i);
    }
}

}; //namespace android
