/*Copyright (c) 2013 - 2015, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "ExtendedUtils"
#include <utils/Log.h>

#include <utils/Errors.h>
#include <sys/types.h>
#include <ctype.h>
#include <unistd.h>
#include <dlfcn.h>

#include <media/stagefright/MetaData.h>
#include <media/stagefright/foundation/ABitReader.h>
#include <media/stagefright/foundation/ABuffer.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/NativeWindowWrapper.h>
#include <media/stagefright/OMXCodec.h>
#include <cutils/properties.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/MediaProfiles.h>
#include <media/stagefright/Utils.h>
#include <camera/ICamera.h>
#include <binder/IPCThreadState.h>

//for Service startup
#include <binder/IBinder.h>
#include <binder/IMemory.h>
#include <binder/Parcel.h>
#include <binder/IServiceManager.h>

//RTSPStream
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include "include/ExtendedUtils.h"

#include <system/window.h>
#include <ui/GraphicBufferMapper.h>

#include <media/AudioParameter.h>
#include <media/AudioSystem.h>
#include <audio_utils/format.h>

extern "C" {
    #include "jpeglib.h"
    #include "jerror.h"
}

#define STACONTROLAPI_LIB "libstaapi.so"

static const int64_t kDefaultAVSyncLateMargin =  40000;
static const int64_t kMaxAVSyncLateMargin     = 250000;
bool android::ExtendedUtils::mIsQCHWAACEncoder = 0;

static const unsigned kDefaultRtpPortRangeStart = 15550;
static const unsigned kDefaultRtpPortRangeEnd = 65535;

static const unsigned kMinRtpPort = 1024;
static const unsigned kMaxRtpPort = 65535;

#define ARG_TOUCH(x) (void)x
static const uint8_t kHEVCNalUnitTypeVidParamSet = 0x20;
static const uint8_t kHEVCNalUnitTypeSeqParamSet = 0x21;
static const uint8_t kHEVCNalUnitTypePicParamSet = 0x22;

#ifdef ENABLE_AV_ENHANCEMENTS

#include <QCMetaData.h>
#include <QCMediaDefs.h>
#include <QOMX_AudioExtensions.h>

#if defined(FLAC_OFFLOAD_ENABLED) || defined(WMA_OFFLOAD_ENABLED) || \
    defined(PCM_OFFLOAD_ENABLED_24) || defined(ALAC_OFFLOAD_ENABLED) || \
    defined(APE_OFFLOAD_ENABLED)
#include "audio_defs.h"
#endif
#include "include/ExtendedExtractor.h"
#include "include/avc_utils.h"

namespace android {

const uint32_t START_SERVICE_TRANSACTION = IBinder::FIRST_CALL_TRANSACTION + 33;
const uint32_t STOP_SERVICE_TRANSACTION  = IBinder::FIRST_CALL_TRANSACTION + 34;

#define AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED  "is_hw_dec_session_allowed"

void ExtendedUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params,
        sp<MetaData> &meta) {
    const char *hfrParam = params.get("video-hfr");
    int32_t hfr = -1;
    if (hfrParam != NULL) {
        hfr = atoi(hfrParam);
        if (hfr > 0) {
            ALOGI("Enabling HFR @ %d fps", hfr);
            meta->setInt32(kKeyHFR, hfr);
            return;
        } else {
            ALOGI("Invalid HFR rate specified : %d", hfr);
        }
    }

    const char *hsrParam = params.get("video-hsr");
    int32_t hsr = -1;
    if (hsrParam != NULL ) {
        hsr = atoi(hsrParam);
        if (hsr > 0) {
            ALOGI("Enabling HSR @ %d fps", hsr);
            meta->setInt32(kKeyHSR, hsr);
        } else {
            ALOGI("Invalid HSR rate specified : %d", hfr);
        }
    }
}

status_t ExtendedUtils::HFR::initializeHFR(
        const sp<MetaData> &meta, sp<AMessage> &format,
        int64_t &maxFileDurationUs, video_encoder videoEncoder) {
    status_t retVal = OK;

    int32_t hsr = 0;
    if (meta->findInt32(kKeyHSR, &hsr) && hsr > 0) {
        ALOGI("HSR cue found. Override encode fps to %d", hsr);
        format->setInt32("frame-rate", hsr);
        return retVal;
    }

    int32_t hfr = 0;
    if (!meta->findInt32(kKeyHFR, &hfr) || (hfr <= 0)) {
        ALOGW("Invalid HFR rate specified");
        return retVal;
    }

    int32_t width = 0, height = 0;
    CHECK(meta->findInt32(kKeyWidth, &width));
    CHECK(meta->findInt32(kKeyHeight, &height));

    int maxW, maxH, MaxFrameRate, maxBitRate = 0;
    if (getHFRCapabilities(videoEncoder,
            maxW, maxH, MaxFrameRate, maxBitRate) < 0) {
        ALOGE("Failed to query HFR target capabilities");
        return ERROR_UNSUPPORTED;
    }

    if ((width * height * hfr) > (maxW * maxH * MaxFrameRate)) {
        ALOGE("HFR request [%d x %d @%d fps] exceeds "
                "[%d x %d @%d fps]. Will stay disabled",
                width, height, hfr, maxW, maxH, MaxFrameRate);
        return ERROR_UNSUPPORTED;
    }

    int32_t frameRate = 0, bitRate = 0;
    CHECK(meta->findInt32(kKeyFrameRate, &frameRate));
    CHECK(format->findInt32("bitrate", &bitRate));

    if (frameRate) {
        // scale the bitrate proportional to the hfr ratio
        // to maintain quality, but cap it to max-supported.
        bitRate = (hfr * bitRate) / frameRate;
        bitRate = bitRate > maxBitRate ? maxBitRate : bitRate;
        format->setInt32("bitrate", bitRate);

        int32_t hfrRatio = hfr / frameRate;
        format->setInt32("frame-rate", hfr);
        format->setInt32("hfr-ratio", hfrRatio);
    } else {
        ALOGE("HFR: Invalid framerate");
        return BAD_VALUE;
    }

    return retVal;
}

void ExtendedUtils::HFR::setHFRRatio(
        sp<MetaData> &meta, const int32_t hfrRatio) {
    if (hfrRatio > 0) {
        meta->setInt32(kKeyHFR, hfrRatio);
    }
}

int32_t ExtendedUtils::HFR::getHFRRatio(
        const sp<MetaData> &meta) {
    int32_t hfrRatio = 0;
    meta->findInt32(kKeyHFR, &hfrRatio);
    return hfrRatio ? hfrRatio : 1;
}

int32_t ExtendedUtils::HFR::getHFRCapabilities(
        video_encoder codec,
        int& maxHFRWidth, int& maxHFRHeight, int& maxHFRFps,
        int& maxBitRate) {
    maxHFRWidth = maxHFRHeight = maxHFRFps = maxBitRate = 0;
    MediaProfiles *profiles = MediaProfiles::getInstance();

    if (profiles) {
        maxHFRWidth = profiles->getVideoEncoderParamByName("enc.vid.hfr.width.max", codec);
        maxHFRHeight = profiles->getVideoEncoderParamByName("enc.vid.hfr.height.max", codec);
        maxHFRFps = profiles->getVideoEncoderParamByName("enc.vid.hfr.mode.max", codec);
        maxBitRate = profiles->getVideoEncoderParamByName("enc.vid.bps.max", codec);
    }

    return (maxHFRWidth > 0) && (maxHFRHeight > 0) &&
            (maxHFRFps > 0) && (maxBitRate > 0) ? 1 : -1;
}

bool ExtendedUtils::HEVCMuxer::isVideoHEVC(const char* mime) {
    return (!strncasecmp(mime, MEDIA_MIMETYPE_VIDEO_HEVC,
                         strlen(MEDIA_MIMETYPE_VIDEO_HEVC)));
}

bool ExtendedUtils::HEVCMuxer::getHEVCCodecConfigData(
                          const sp<MetaData> &meta, const void **data,
                          size_t *size) {
    uint32_t type;
    ALOGV("getHEVCCodecConfigData called");
    return meta->findData(kKeyHVCC, &type, data, size);
}

void ExtendedUtils::HEVCMuxer::writeHEVCFtypBox(MPEG4Writer *writer) {
    ALOGV("writeHEVCFtypBox called");
    writer->writeFourcc("3gp5");
    writer->writeInt32(0);
    writer->writeFourcc("hvc1");
    writer->writeFourcc("hev1");
    writer->writeFourcc("3gp5");
}

void ExtendedUtils::HEVCMuxer::beginHEVCBox(MPEG4Writer *writer) {
    ALOGV("beginHEVCBox called");
    writer->beginBox("hvc1");
}

void ExtendedUtils::HEVCMuxer::writeHvccBox(MPEG4Writer *writer,
                                            void* codecSpecificData,
                                            size_t codecSpecificDataSize,
                                            bool useNalLengthFour) {
    ALOGV("writeHvccBox called");
    CHECK(codecSpecificData);
    CHECK_GE(codecSpecificDataSize, 23);

    // Patch hvcc's lengthSize field to match the number
    // of bytes we use to indicate the size of a nal unit.
    uint8_t *ptr = (uint8_t *)codecSpecificData;
    ptr[21] = (ptr[21] & 0xfc) | (useNalLengthFour? 3 : 1);
    writer->beginBox("hvcC");
    writer->write(codecSpecificData, codecSpecificDataSize);
    writer->endBox();  // hvcC
}

status_t ExtendedUtils::HEVCMuxer::extractNALRBSPData(const uint8_t *data,
                                            size_t size,
                                            uint8_t **header,
                                            bool *alreadyFilled) {
    ALOGV("extractNALRBSPData called");
    CHECK_GE(size, 2);

    uint8_t type = data[0] >> 1;
    type = 0x3f & type;

    //start parsing here
    size_t rbspSize = 0;
    uint8_t *rbspData = (uint8_t *) malloc(size);

    if (rbspData == NULL) {
        ALOGE("allocation failed");
        return UNKNOWN_ERROR;
    }

    //populate rbsp data start from i+2, search for 0x000003,
    //and ignore emulation_prevention byte
    size_t itt = 2;
    while (itt < size) {
        if ((itt+2 < size) && (!memcmp("\x00\x00\x03", &data[itt], 3) )) {
            rbspData[rbspSize++] = data[itt++];
            rbspData[rbspSize++] = data[itt++];
            itt++;
        } else {
            rbspData[rbspSize++] = data[itt++];
        }
    }

    uint8_t maxSubLayerMinus1 = 0;

    //parser profileTierLevel
    if (type == kHEVCNalUnitTypeVidParamSet) { // if VPS
        ALOGV("its VPS ... start with 5th byte");
        if (rbspSize < 5) {
            free(rbspData);
            return ERROR_MALFORMED;
        }

        maxSubLayerMinus1 = 0x0E & rbspData[1];
        maxSubLayerMinus1 = maxSubLayerMinus1 >> 1;
        parserProfileTierLevel(&rbspData[4], rbspSize - 4, header, alreadyFilled);

    } else if (type == kHEVCNalUnitTypeSeqParamSet) {
        ALOGV("its SPS .. start with 2nd byte");
        if (rbspSize < 2) {
            free(rbspData);
            return ERROR_MALFORMED;
        }

        maxSubLayerMinus1 = 0x0E & rbspData[0];
        maxSubLayerMinus1 = maxSubLayerMinus1 >> 1;

        parserProfileTierLevel(&rbspData[1], rbspSize - 1, header, alreadyFilled);
    }
    free(rbspData);
    return OK;
}

status_t ExtendedUtils::HEVCMuxer::parserProfileTierLevel(const uint8_t *data, size_t size,
                                                     uint8_t **header, bool *alreadyFilled) {
    CHECK_GE(size, 12);
    uint8_t *tmpHeader = *header;
    ALOGV("parserProfileTierLevel called");
    uint8_t generalProfileSpace; //2 bit
    uint8_t generalTierFlag;     //1 bit
    uint8_t generalProfileIdc;   //5 bit
    uint8_t generalProfileCompatibilityFlag[4];
    uint8_t generalConstraintIndicatorFlag[6];
    uint8_t generalLevelIdc;     //8 bit

    // Need first 12 bytes

    // First byte will give below info
    generalProfileSpace = 0xC0 & data[0];
    generalProfileSpace = generalProfileSpace > 6;
    generalTierFlag = 0x20 & data[0];
    generalTierFlag = generalTierFlag > 5;
    generalProfileIdc = 0x1F & data[0];

    // Next 4 bytes is compatibility flag
    memcpy(&generalProfileCompatibilityFlag, &data[1], 4);

    // Next 6 bytes is constraint indicator flag
    memcpy(&generalConstraintIndicatorFlag, &data[5], 6);

    // Next 1 byte is general Level IDC
    generalLevelIdc = data[11];

    if (*alreadyFilled) {
        bool overwriteTierValue = false;

        //find profile space
        uint8_t prvGeneralProfileSpace; //2 bit
        prvGeneralProfileSpace = 0xC0 & tmpHeader[1];
        prvGeneralProfileSpace = prvGeneralProfileSpace > 6;
        //prev needs to be same as current
        if (prvGeneralProfileSpace != generalProfileSpace) {
            ALOGW("Something wrong!!! profile space mismatch");
        }

        uint8_t prvGeneralTierFlag = 0x20 & tmpHeader[1];
        prvGeneralTierFlag = prvGeneralTierFlag > 5;

        if (prvGeneralTierFlag < generalTierFlag) {
            overwriteTierValue = true;
            ALOGV("Found higher tier value, replacing old one");
        }

        uint8_t prvGeneralProfileIdc = 0x1F & tmpHeader[1];

        if (prvGeneralProfileIdc != generalProfileIdc) {
            ALOGW("Something is wrong!!! profile space mismatch");
        }

        if (overwriteTierValue) {
            tmpHeader[1] = data[0];
        }

        //general level IDC should be set highest among all
        if (tmpHeader[12] < data[11]) {
            tmpHeader[12] = data[11];
            ALOGV("Found higher level IDC value, replacing old one");
        }

    } else {
        *alreadyFilled = true;
        tmpHeader[1] = data[0];
        memcpy(&tmpHeader[2], &data[1], 4);
        memcpy(&tmpHeader[6], &data[5], 6);
        tmpHeader[12] = data[11];
    }

    char printCodecConfig[PROPERTY_VALUE_MAX];
    property_get("hevc.mux.print.codec.config", printCodecConfig, "0");

    if (atoi(printCodecConfig)) {
        //if property enabled, print these values
        ALOGI("Start::-----------------");
        ALOGI("generalProfileSpace = %2x", generalProfileSpace);
        ALOGI("generalTierFlag     = %2x", generalTierFlag);
        ALOGI("generalProfileIdc   = %2x", generalProfileIdc);
        ALOGI("generalLevelIdc     = %2x", generalLevelIdc);
        ALOGI("generalProfileCompatibilityFlag = %2x %2x %2x %2x", generalProfileCompatibilityFlag[0],
               generalProfileCompatibilityFlag[1], generalProfileCompatibilityFlag[2],
               generalProfileCompatibilityFlag[3]);
        ALOGI("generalConstraintIndicatorFlag = %2x %2x %2x %2x %2x %2x", generalConstraintIndicatorFlag[0],
               generalConstraintIndicatorFlag[1], generalConstraintIndicatorFlag[2],
               generalConstraintIndicatorFlag[3], generalConstraintIndicatorFlag[4],
               generalConstraintIndicatorFlag[5]);
        ALOGI("End::-----------------");
    }

    return OK;
}

static const uint8_t *findNextStartCode(
       const uint8_t *data, size_t length) {
    ALOGV("findNextStartCode: %p %d", data, length);

    size_t bytesLeft = length;

    while (bytesLeft > 4 &&
            memcmp("\x00\x00\x00\x01", &data[length - bytesLeft], 4)) {
        --bytesLeft;
    }

    if (bytesLeft <= 4) {
        bytesLeft = 0; // Last parameter set
    }

    return &data[length - bytesLeft];
}

const uint8_t *ExtendedUtils::HEVCMuxer::parseHEVCParamSet(
        const uint8_t *data, size_t length, List<HEVCParamSet> &paramSetList, size_t *paramSetLen) {
    ALOGV("parseHEVCParamSet called");
    const uint8_t *nextStartCode = findNextStartCode(data, length);
    *paramSetLen = nextStartCode - data;
    if (*paramSetLen == 0) {
        ALOGE("Param set is malformed, since its length is 0");
        return NULL;
    }

    HEVCParamSet paramSet(*paramSetLen, data);
    paramSetList.push_back(paramSet);

    return nextStartCode;
}

static void getHEVCNalUnitType(uint8_t byte, uint8_t* type) {
    ALOGV("getNalUnitType: %d", (int)byte);
    // nal_unit_type: 6-bit unsigned integer
    *type = (byte & 0x7E) >> 1;
}

size_t ExtendedUtils::HEVCMuxer::parseHEVCCodecSpecificData(
        const uint8_t *data, size_t size,List<HEVCParamSet> &vidParamSet,
        List<HEVCParamSet> &seqParamSet, List<HEVCParamSet> &picParamSet ) {
    ALOGV("parseHEVCCodecSpecificData called");
    // Data starts with a start code.
    // VPS, SPS and PPS are separated with start codes.
    uint8_t type = kHEVCNalUnitTypeVidParamSet;
    bool gotVps = false;
    bool gotSps = false;
    bool gotPps = false;
    const uint8_t *tmp = data;
    const uint8_t *nextStartCode = data;
    size_t bytesLeft = size;
    size_t paramSetLen = 0;
    size_t codecSpecificDataSize = 0;
    while (bytesLeft > 4 && !memcmp("\x00\x00\x00\x01", tmp, 4)) {
        getHEVCNalUnitType(*(tmp + 4), &type);
        if (type == kHEVCNalUnitTypeVidParamSet) {
            nextStartCode = parseHEVCParamSet(tmp + 4, bytesLeft - 4, vidParamSet, &paramSetLen);
            if (!gotVps) {
                gotVps = true;
            }
        } else if (type == kHEVCNalUnitTypeSeqParamSet) {
            nextStartCode = parseHEVCParamSet(tmp + 4, bytesLeft - 4, seqParamSet, &paramSetLen);
            if (!gotSps) {
                gotSps = true;
            }

        } else if (type == kHEVCNalUnitTypePicParamSet) {
            nextStartCode = parseHEVCParamSet(tmp + 4, bytesLeft - 4, picParamSet, &paramSetLen);
            if (!gotPps) {
                gotPps = true;
            }
        } else {
            ALOGE("Only VPS, SPS and PPS Nal units are expected");
            return ERROR_MALFORMED;
        }

        if (nextStartCode == NULL) {
            ALOGE("Next start code is NULL");
            return ERROR_MALFORMED;
        }

        // Move on to find the next parameter set
        bytesLeft -= nextStartCode - tmp;
        tmp = nextStartCode;
        codecSpecificDataSize += (2 + paramSetLen);
    }

#if 0
//not adding this check now, but might be needed
    if (!gotVps || !gotVps || !gotVps ) {
        return 0;
    }
#endif

    return codecSpecificDataSize;
}

status_t ExtendedUtils::HEVCMuxer::makeHEVCCodecSpecificData(
                         const uint8_t *data, size_t size, void** codecSpecificData,
                         size_t *codecSpecificDataSize) {
    ALOGV("makeHEVCCodecSpecificData called");

    if (*codecSpecificData != NULL) {
        ALOGE("Already have codec specific data");
        return ERROR_MALFORMED;
    }

    if (size < 4) {
        ALOGE("Codec specific data length too short: %zu", size);
        return ERROR_MALFORMED;
    }

    // Data is in the form of HVCCodecSpecificData
    if (memcmp("\x00\x00\x00\x01", data, 4)) {
        // 23 byte fixed header
        if (size < 23) {
            ALOGE("Codec specific data length too short: %zu", size);
            return ERROR_MALFORMED;
        }

        *codecSpecificData = malloc(size);

        if (*codecSpecificData != NULL) {
            *codecSpecificDataSize = size;
            memcpy(*codecSpecificData, data, size);
            return OK;
        }

        return NO_MEMORY;
    }

    List<HEVCParamSet> vidParamSets;
    List<HEVCParamSet> seqParamSets;
    List<HEVCParamSet> picParamSets;

    if ((*codecSpecificDataSize = parseHEVCCodecSpecificData(data, size,
                                   vidParamSets, seqParamSets, picParamSets)) <= 0) {
        ALOGE("cannot parser codec specific data, bailing out");
        return ERROR_MALFORMED;
    }

    size_t numOfNALArray = 0;
    bool doneWritingVPS = true, doneWritingSPS = true, doneWritingPPS = true;

    if (!vidParamSets.empty()) {
        doneWritingVPS = false;
        ++numOfNALArray;
    }

    if (!seqParamSets.empty()) {
       doneWritingSPS = false;
       ++numOfNALArray;
    }

    if (!picParamSets.empty()) {
       doneWritingPPS = false;
       ++numOfNALArray;
    }

    //additional 23 bytes needed (22 bytes for hvc1 header + 1 byte for number of arrays)
    *codecSpecificDataSize += 23;
    //needed 3 bytes per NAL array
    *codecSpecificDataSize += 3 * numOfNALArray;

    int count = 0;
    void *codecConfigData = malloc(*codecSpecificDataSize);
    if (codecConfigData == NULL) {
        ALOGE("Failed to allocate memory, bailing out");
        return NO_MEMORY;
    }

    uint8_t *header = (uint8_t *)codecConfigData;
    // 8  - bit version
    header[0]  = 1;
    //Profile space 2 bit, tier flag 1 bit and profile IDC 5 bit
    header[1]  = 0x00;
    // 32 - bit compatibility flag
    header[2]  = 0x00;
    header[3]  = 0x00;
    header[4]  = 0x00;
    header[5]  = 0x00;
    // 48 - bit general constraint indicator flag
    header[6]  = header[7]  = header[8]  = 0x00;
    header[9]  = header[10] = header[11] = 0x00;
    // 8  - bit general IDC level
    header[12] = 0x00;
    // 4  - bit reserved '1111'
    // 12 - bit spatial segmentation idc
    header[13] = 0xf0;
    header[14] = 0x00;
    // 6  - bit reserved '111111'
    // 2  - bit parallelism Type
    header[15] = 0xfc;
    // 6  - bit reserved '111111'
    // 2  - bit chromaFormat
    header[16] = 0xfc;
    // 5  - bit reserved '11111'
    // 3  - bit DepthLumaMinus8
    header[17] = 0xf8;
    // 5  - bit reserved '11111'
    // 3  - bit DepthChromaMinus8
    header[18] = 0xf8;
    // 16 - bit average frame rate
    header[19] = header[20] = 0x00;
    // 2  - bit constant frame rate
    // 3  - bit num temporal layers
    // 1  - bit temoral nested
    // 2  - bit lengthSizeMinusOne
    header[21] = 0x07;

    // 8-bit number of NAL types
    header[22] = (uint8_t)numOfNALArray;

    header += 23;
    count  += 23;

    bool ifProfileIDCAlreadyFilled = false;

    if (!doneWritingVPS) {
        doneWritingVPS = true;
        ALOGV("Writing VPS");
        //8-bit, last 6 bit for NAL type
        header[0] = 0x20; // NAL type is VPS
        //16-bit, number of nal Units
        uint16_t vidParamSetLength = vidParamSets.size();
        header[1] = vidParamSetLength >> 8;
        header[2] = vidParamSetLength && 0xff;

        header += 3;
        count  += 3;

        for (List<HEVCParamSet>::iterator it = vidParamSets.begin();
            it != vidParamSets.end(); ++it) {
            // 16-bit video parameter set length
            uint16_t vidParamSetLength = it->mLength;
            header[0] = vidParamSetLength >> 8;
            header[1] = vidParamSetLength & 0xff;

            extractNALRBSPData(it->mData, it->mLength,
                               (uint8_t **)&codecConfigData,
                               &ifProfileIDCAlreadyFilled);

            // VPS NAL unit (video parameter length bytes)
            memcpy(&header[2], it->mData, vidParamSetLength);
            header += (2 + vidParamSetLength);
            count  += (2 + vidParamSetLength);
        }
    }

    if (!doneWritingSPS) {
        doneWritingSPS = true;
        ALOGV("Writting SPS");
        //8-bit, last 6 bit for NAL type
        header[0] = 0x21; // NAL type is SPS
        //16-bit, number of nal Units
        uint16_t seqParamSetLength = seqParamSets.size();
        header[1] = seqParamSetLength >> 8;
        header[2] = seqParamSetLength && 0xff;

        header += 3;
        count  += 3;

        for (List<HEVCParamSet>::iterator it = seqParamSets.begin();
              it != seqParamSets.end(); ++it) {
            // 16-bit sequence parameter set length
            uint16_t seqParamSetLength = it->mLength;

            // 16-bit number of NAL units of this type
            header[0] = seqParamSetLength >> 8;
            header[1] = seqParamSetLength & 0xff;

            extractNALRBSPData(it->mData, it->mLength,
                               (uint8_t **)&codecConfigData,
                               &ifProfileIDCAlreadyFilled);

            // SPS NAL unit (sequence parameter length bytes)
            memcpy(&header[2], it->mData, seqParamSetLength);
            header += (2 + seqParamSetLength);
            count  += (2 + seqParamSetLength);
        }
    }

    if (!doneWritingPPS) {
        doneWritingPPS = true;
        ALOGV("writing PPS");
        //8-bit, last 6 bit for NAL type
        header[0] = 0x22; // NAL type is PPS
        //16-bit, number of nal Units
        uint16_t picParamSetLength = picParamSets.size();
        header[1] = picParamSetLength >> 8;
        header[2] = picParamSetLength && 0xff;

        header += 3;
        count  += 3;

        for (List<HEVCParamSet>::iterator it = picParamSets.begin();
             it != picParamSets.end(); ++it) {
            // 16-bit picture parameter set length
            uint16_t picParamSetLength = it->mLength;
            header[0] = picParamSetLength >> 8;
            header[1] = picParamSetLength & 0xff;

            // PPS Nal unit (picture parameter set length bytes)
            memcpy(&header[2], it->mData, picParamSetLength);
            header += (2 + picParamSetLength);
            count  += (2 + picParamSetLength);
        }
    }
    *codecSpecificData = codecConfigData;
    return OK;
}

bool ExtendedUtils::ShellProp::isAudioDisabled(bool isEncoder) {
    bool retVal = false;
    char disableAudio[PROPERTY_VALUE_MAX];
    property_get("persist.debug.sf.noaudio", disableAudio, "0");
    if (isEncoder && (atoi(disableAudio) & 0x02)) {
        retVal = true;
    } else if (atoi(disableAudio) & 0x01) {
        retVal = true;
    }
    return retVal;
}

void ExtendedUtils::ShellProp::setEncoderProfile(
        video_encoder &videoEncoder, int32_t &videoEncoderProfile,
        int32_t &videoEncoderLevel) {
    char value[PROPERTY_VALUE_MAX];
    if (!property_get("encoder.video.profile", value, NULL) > 0) {
        return;
    }

    int32_t profile = videoEncoderProfile;
    int32_t level = videoEncoderLevel;
    switch (videoEncoder) {
        case VIDEO_ENCODER_H264:
            // Set the minimum valid level if the level was undefined;
            // encoder will choose the right level anyways
            level = (level < 0) ? OMX_VIDEO_AVCLevel1 : level;
            if (strncmp("base", value, 4) == 0) {
                profile = OMX_VIDEO_AVCProfileBaseline;
                ALOGI("H264 Baseline Profile");
            } else if (strncmp("main", value, 4) == 0) {
                profile = OMX_VIDEO_AVCProfileMain;
                ALOGI("H264 Main Profile");
            } else if (strncmp("high", value, 4) == 0) {
                profile = OMX_VIDEO_AVCProfileHigh;
                ALOGI("H264 High Profile");
            } else {
                ALOGW("Unsupported H264 Profile");
            }
            break;
        case VIDEO_ENCODER_MPEG_4_SP:
            level = (level < 0) ? OMX_VIDEO_MPEG4Level0 : level;
            if (strncmp("simple", value, 5) == 0 ) {
                profile = OMX_VIDEO_MPEG4ProfileSimple;
                ALOGI("MPEG4 Simple profile");
            } else if (strncmp("asp", value, 3) == 0 ) {
                profile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                ALOGI("MPEG4 Advanced Simple Profile");
            } else {
                ALOGW("Unsupported MPEG4 Profile");
            }
            break;
        default:
            ALOGW("No custom profile support for other codecs");
            break;
    }
    // Override _both_ profile and level, only if they are valid
    if (profile && level) {
        videoEncoderProfile = profile;
        videoEncoderLevel = level;
    }
}

int64_t ExtendedUtils::ShellProp::getMaxAVSyncLateMargin() {
    char lateMarginMs[PROPERTY_VALUE_MAX] = {0};
    property_get("media.sf.set.late.margin", lateMarginMs, "0");
    int64_t newLateMarginUs = atoi(lateMarginMs)*1000;
    int64_t maxLateMarginUs = newLateMarginUs;

    if (newLateMarginUs > kDefaultAVSyncLateMargin
            || newLateMarginUs < kDefaultAVSyncLateMargin) {
        maxLateMarginUs = kDefaultAVSyncLateMargin;
    }

    ALOGI("AV Sync late margin : Intended=%lldms Using=%lldms",
            maxLateMarginUs/1000, newLateMarginUs/1000);
    return maxLateMarginUs;
}

bool ExtendedUtils::ShellProp::isSmoothStreamingEnabled() {
    char prop[PROPERTY_VALUE_MAX] = {0};
    property_get("mm.enable.smoothstreaming", prop, "0");
    if (!strncmp(prop, "true", 4) || atoi(prop)) {
        return true;
    }
    return false;
}

void ExtendedUtils::ShellProp::getRtpPortRange(unsigned *start, unsigned *end) {
    char value[PROPERTY_VALUE_MAX];
    if (!property_get("persist.sys.media.rtp-ports", value, NULL)) {
        ALOGV("Cannot get property of persist.sys.media.rtp-ports");
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    if (sscanf(value, "%u-%u", start, end) != 2) {
        ALOGE("Failed to parse rtp port range from '%s'.", value);
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    if (*start > *end || *start <= kMinRtpPort || *end >= kMaxRtpPort) {
        ALOGE("Illegal rtp port start/end specified, reverting to defaults.");
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    ALOGV("rtp port_start = %u, port_end = %u", *start, *end);
}

wp<ExtendedUtils::DiscoverProxy> ExtendedUtils::DiscoverProxy::gDProxy = NULL;
Mutex ExtendedUtils::DiscoverProxy::gLock;
bool ExtendedUtils::DiscoverProxy::gSetStopProxyInProgress = false;
Condition ExtendedUtils::DiscoverProxy::gCondition;
sp<ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient> ExtendedUtils::DiscoverProxy::gDeathNotifier = NULL;

ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient::~STAProxyServiceDeathRecepient() {
    ALOGV("~STAProxyServiceDeathRecepient()");
}

void
ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient::binderDied(const wp<IBinder>& who) {
    ALOGI("STAPrxoyService sDeathNotifierService %p died!", who.unsafe_get());
    gSetStopProxyInProgress = false;
    gCondition.signal();
}


void ExtendedUtils::DiscoverProxy::create(
        sp<ExtendedUtils::DiscoverProxy> *proxy,
        KeyedVector<String8, String8> *hdr,
        const KeyedVector<String8, String8> *headers) {
    CHECK(hdr);
    CHECK(proxy);
    if (headers != NULL) {
        *hdr = *headers;
    }

    // Disable STAProxy usage for custom hls specific usecases,
    // Not starting STAProxy if HLS specific custom
    // property is enabled
    if (true == ExtendedUtils::ShellProp::isCustomHLSEnabled()) {
        return;
    }

    if ((*proxy) != NULL) {
       ALOGI("Proxy already detected, pls reuse it");
       return;
    }

    *proxy = ExtendedUtils::DiscoverProxy::create();
    if ((*proxy) != NULL) {
        // discover-proxy key signals lower layers to
        // discover and set proxy port on stack before connect
        hdr->add(String8("discover-proxy"), String8(""));
    }
}

status_t ExtendedUtils::DiscoverProxy::checkForProxyAvail(
         sp<ExtendedUtils::DiscoverProxy> proxy,
         KeyedVector<String8, String8> *headers, int32_t *count,
         uint32_t what, const AString& url, ALooper::handler_id target) {
    CHECK(headers);
    CHECK(count);
    status_t err = OK;
    int64_t delayUs = 0;
    if ((proxy != NULL) &&
        ((err = proxy->checkProxyAvail(headers, count, &delayUs)) != OK)) {
        if (err == -EAGAIN) {
            sp<AMessage> msg = new AMessage(what, target);
            msg->setString("url", url);
            msg->setPointer("headers", headers);
            msg->post(delayUs);
            return err;
         } else {
            ALOGI("Failed checkProxyAvail continue without proxy %ld", err);
         }
     }

     return err;
}

status_t ExtendedUtils::DiscoverProxy::checkProxyAvail(
         KeyedVector<String8, String8> *headers,
         int32_t *count, int64_t *delayUs) {
    CHECK(headers);
    CHECK(count);
    CHECK(delayUs);
    if ((headers->indexOfKey(String8("discover-proxy"))) < 0) {
        return OK;
    }

    (*count)++;
    int32_t port = 0;
    if (getSTAProxyConfig(port)) {
        ALOGI("Detected Proxy at port %d in %d attempts", port, *count);
        String8 portString = String8("127.0.0.1");
        portString.appendFormat(":%d", port);
        ALOGV("getSTAProxyConfig Proxy IPportString %s", portString.string());
        headers->add(String8("use-proxy"), portString);
        *count = 0;
        ssize_t index;
        if ((index = headers->indexOfKey(String8("discover-proxy"))) >= 0) {
             headers->removeItemsAt(index);
        }
        return OK;
    } else if ((*count) > MAX_CHECK_PROXY_RETRY_COUNT) {
        ALOGI("Not Detected proxy in %d attempts, bypass proxy", *count);
        ssize_t index;
        if ((index = headers->indexOfKey(String8("discover-proxy"))) >= 0) {
             headers->removeItemsAt(index);
        }
        return OK;
    } else {
        *delayUs = kProxyPollDelayUs;
        ALOGV("Repost to query proxy count %d", *count);
        return -EAGAIN;
    }
}

bool ExtendedUtils::DiscoverProxy::isProxySet(
   const KeyedVector<String8, String8>& headers) {
   return (headers.indexOfKey(String8("use-proxy")) >= 0);
}

sp<ExtendedUtils::DiscoverProxy> ExtendedUtils::DiscoverProxy::create() {
   Mutex::Autolock autoLock(gLock);
   sp<DiscoverProxy> instance = gDProxy.promote();
   if(instance != NULL) {
      ALOGW("DiscoverProxy reuse instance");
      return instance;
   }

   char value[PROPERTY_VALUE_MAX];
   property_get("persist.mm.sta.enable", value, "0");
   // Return NULL if persist.mm.sta.enable is set to 0
   if (!atoi(value)) {
        ALOGW("Proxy is disabled using persist.mm.sta.enable 0");
        return NULL;
   }

   ALOGW("DiscoverProxy create instance");
   bool bOk = false;
   instance = new DiscoverProxy(bOk);
   if((instance == NULL) || (false == bOk)) {
      ALOGE("DiscoverProxy failed to create instance");
      return NULL;
   }

   char val[PROPERTY_VALUE_MAX];
   property_get("persist.mm.sta.start.bootup", val, "0");
   if (!atoi(val)) {
       sendSTAProxyStartIntent();
   }

   gDProxy = instance;
   return instance;
}

ExtendedUtils::DiscoverProxy::DiscoverProxy(bool& bOk)
    : mStaLibHandle(NULL),
      isProxySupported(NULL),
      getPort(NULL) {

    bOk = true;
    mStaLibHandle = dlopen(STACONTROLAPI_LIB, RTLD_NOW);
    if (mStaLibHandle == NULL) {
        ALOGE("libstaapi.so open dll error :%s", dlerror());
        bOk = false;
    }

    if (bOk) {
        isProxySupported = (fnIsProxySupported) dlsym(mStaLibHandle, "isSTAProxySupported");
        if (isProxySupported == NULL) {
            ALOGE("Not able to load the symbol");
            bOk = false;
        }
    }

    if (bOk) {
        getPort = (fnGetPort) dlsym(mStaLibHandle, "getSTAProxyAlwaysAccelerateServicePort");
        if (getPort == NULL) {
            ALOGE("Not able to load the symbol to get the STA proxy port");
            bOk = false;
        }
    }

    ALOGW("DiscoverProxy ExtendedUtils::DiscoverProxy::DiscoverProxy() bOk %d", bOk);
}

ExtendedUtils::DiscoverProxy::~DiscoverProxy() {
    Mutex::Autolock autoLock(gLock);
    if (mStaLibHandle != NULL) {
        dlclose(mStaLibHandle);

        char val[PROPERTY_VALUE_MAX];
        property_get("persist.mm.sta.start.bootup", val, "0");
        if (!atoi(val)) {
            sendSTAProxyStopIntent();
        }
    }

    gDProxy = NULL;
    ALOGW("DiscoverProxy ExtendedUtils::DiscoverProxy::~DiscoverProxy()");
}

bool ExtendedUtils::DiscoverProxy::sendSTAProxyStartIntent() {
    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> am = sm->getService(String16("activity"));
    if (am == NULL) {
        ALOGE("startServiceThroughActivityManager() couldn't find activity service!\n");
        return false;
    }

    Parcel data, reply;

    data.writeInterfaceToken(String16("android.app.IActivityManager"));
    data.writeStrongBinder(NULL); /* caller */

    /* intent */
    data.writeString16(NULL, 0); /* action */
    data.writeInt32(0); /* Uri - data */
    data.writeString16(NULL,0); /* Uri - type */
    data.writeInt32(0); /* flags */
    data.writeString16(NULL,0); /* package name */
    data.writeString16(String16("com.qualcomm.sta")); /* Component name reads the package name again */
    data.writeString16(String16("com.qualcomm.sta.STAProxyService")); /* component name */
    data.writeInt32(0); /* source bound - size */
    data.writeInt32(0); /* Categories - size */
    data.writeInt32(0); /* selector - size */
    data.writeInt32(0); /* ClipData */
    data.writeInt32(0); /* root user */
    data.writeInt32(-1); /* bundle(extras) size */
    /* end of intent */

    //ResolveType
    data.writeInt32(-1); // "resolvedType" String16 (null)

    data.writeInt32(0); /* root user */

    status_t ret = am->transact(START_SERVICE_TRANSACTION, data, &reply, 0);
    ALOGI("ExtendedUtils::DiscoverProxy::Sent STAProxy Service start Intent");
    return true;
}

bool ExtendedUtils::DiscoverProxy::sendSTAProxyStopIntent() {
    sp<IServiceManager> sm1 = defaultServiceManager();
    sp<IBinder> am1 = sm1->getService(String16("STAProxyService"));
    if (am1 == NULL) {
        ALOGE("sendSTAProxyStopIntent couldn't find STAProxyService service!\n");
    } else {
        gDeathNotifier = new STAProxyServiceDeathRecepient();
        am1->linkToDeath(gDeathNotifier);

        gSetStopProxyInProgress = true;
        ALOGV("STAProxyService found and linked to deathnotifier!\n");
    }

    sp<IServiceManager> sm = defaultServiceManager();
    sp<IBinder> am = sm->getService(String16("activity"));
    if (am == NULL) {
        ALOGE("startServiceThroughActivityManager() couldn't find activity service!\n");
        return false;
    }

    Parcel data, reply;

    data.writeInterfaceToken(String16("android.app.IActivityManager"));
    data.writeStrongBinder(NULL); /* caller */

    /* intent */
    data.writeString16(NULL, 0); /* action */
    data.writeInt32(0); /* Uri - data */
    data.writeString16(NULL,0); /* Uri - type */
    data.writeInt32(0); /* flags */
    data.writeString16(NULL,0); /* package name */
    data.writeString16(String16("com.qualcomm.sta")); /* Component name reads the package name again */
    data.writeString16(String16("com.qualcomm.sta.STAProxyService")); /* component name */
    data.writeInt32(0); /* source bound - size */
    data.writeInt32(0); /* Categories - size */
    data.writeInt32(0); /* selector - size */
    data.writeInt32(0); /* ClipData */
    data.writeInt32(0); /* root user */
    data.writeInt32(-1); /* bundle(extras) size */
    /* end of intent */
    //ResolveType
    data.writeInt32(-1); // "resolvedType" String16 (null)

    data.writeInt32(0); /* root user */

    status_t ret = am->transact(STOP_SERVICE_TRANSACTION, data, &reply, 0);
    ALOGI("ExtendedUtils::DiscoverProxy::Sent STAProxy Service stop Intent");

    if (gSetStopProxyInProgress) {
        while (gSetStopProxyInProgress) {
           status_t err = gCondition.waitRelative(gLock, kBinderDieTimeoutNs);
            if (err == -ETIMEDOUT){
                ALOGE(" STAProxy service stop timed out for %lld nanoseconds", kBinderDieTimeoutNs);
                if (am1->isBinderAlive() && (gDeathNotifier != NULL)) {
                    am1->unlinkToDeath(gDeathNotifier);
                }
                gSetStopProxyInProgress = false;
                break;
            }
        }
        ALOGI("STAProxyService completely stopped!\n");
    }

    return true;
}

bool ExtendedUtils::DiscoverProxy::getSTAProxyConfig(int32_t &port) {
    Mutex::Autolock autoLock(gLock);
    char value[PROPERTY_VALUE_MAX];
    property_get("persist.mm.sta.enable", value, "0");
    // Return false if persist.mm.sta.enable is set to 0
    if (!atoi(value)) {
        ALOGW("Proxy is disabled using persist.mm.sta.enable 0");
        return false;
    }
    if ((isProxySupported == NULL) || (getPort == NULL)) {
        ALOGW("Invalid symbols isProxySupported %p, getPort %p", isProxySupported, getPort);
        return false;
    }
    if (isProxySupported()) {
        port = getPort();
        if (port > 0) {
            ALOGI("The STA proxy is running at port:%d", port );
        } else {
            ALOGI("The STA proxy is running at invalid port:%d", port );
            return false;
        }
    } else {
        ALOGW("STA Proxy is not supported");
        return false;
    }

    return true;
}

bool ExtendedUtils::ShellProp::getSTAProxyConfig(int32_t &port) {
    void* staLibHandle = NULL;

    char value[PROPERTY_VALUE_MAX];
    property_get("persist.mm.sta.enable", value, "0");
    // Return false if persist.mm.sta.enable is set to 0
    if (!atoi(value)) {
        ALOGW("Proxy is disabled using persist.mm.sta.enable");
        return false;
    }

    staLibHandle = dlopen("libstaapi.so", RTLD_NOW);
    if (staLibHandle == NULL) {
        ALOGW("libstaapi.so open dll error :%s", dlerror());
        return false;
    }
    typedef bool (*fnIsProxySupported)();
    typedef int (*fnGetPort)();

    fnIsProxySupported isProxySupported = (fnIsProxySupported) dlsym(staLibHandle, "isSTAProxySupported");
    if (isProxySupported == NULL) {
        ALOGW("Not able to load the symbol");
        dlclose(staLibHandle);
        return false;
    }
    if (isProxySupported()) {
        fnGetPort getPort = (fnGetPort)dlsym(staLibHandle, "getSTAProxyAlwaysAccelerateServicePort");
        if (getPort == NULL) {
            ALOGW("Not able to load the symbol to get the STA proxy port");
            dlclose(staLibHandle);
            return false;
        }
        port = getPort();
        ALOGI("The STA proxy is running at port:%d", port );
    } else {
        ALOGW("STA Proxy is not supported");
        dlclose(staLibHandle);
        return false;
    }
    if (staLibHandle != NULL) {
        dlclose(staLibHandle);
    }
    return true;
}

bool ExtendedUtils::ShellProp::isCustomHLSEnabled() {
    bool retVal = false;
    char customHLS[PROPERTY_VALUE_MAX];
    property_get("persist.sys.media.hls-custom", customHLS, "0");
    if (atoi(customHLS)) {
        retVal = true;
    }
    return retVal;
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, const char* componentName) {
    //ignore non QC components
    if (strncmp(componentName, "OMX.qcom.", 9)) {
        return;
    }
    if (mpeg4type.eProfile > OMX_VIDEO_MPEG4ProfileSimple) {
        mpeg4type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
        mpeg4type.nPFrames = (mpeg4type.nPFrames + kNumBFramesPerPFrame) /
                (kNumBFramesPerPFrame + 1);
        mpeg4type.nBFrames = mpeg4type.nPFrames * kNumBFramesPerPFrame;
    }
    return;
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, const int32_t iFramesInterval,
        const int32_t frameRate, const char* componentName) {
    //ignore non QC components
    if (strncmp(componentName, "OMX.qcom.", 9)) {
        return;
    }
    OMX_U32 val = 0;
    if (iFramesInterval < 0) {
        val =  0xFFFFFFFF;
    } else if (iFramesInterval == 0) {
        val = 0;
    } else {
        val  = frameRate * iFramesInterval - 1;
        CHECK(val > 1);
    }

    h264type.nPFrames = val;

    if (h264type.nPFrames == 0) {
        h264type.nAllowedPictureTypes = OMX_VIDEO_PictureTypeI;
    }

    if (h264type.eProfile > OMX_VIDEO_AVCProfileBaseline) {
        h264type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
        h264type.nPFrames = (h264type.nPFrames + kNumBFramesPerPFrame) /
                (kNumBFramesPerPFrame + 1);
        h264type.nBFrames = h264type.nPFrames * kNumBFramesPerPFrame;

        //enable CABAC as default entropy mode for High/Main profiles
        h264type.bEntropyCodingCABAC = OMX_TRUE;
        h264type.nCabacInitIdc = 0;
    }
    return;
}

sp<MetaData> ExtendedUtils::updatePCMFormatAndBitwidth(
                sp<MediaSource> &audioSource, bool offloadAudio)
{
    sp<MetaData> tempMetadata = new MetaData;
    sp<MetaData> format = audioSource->getFormat();
    int bitWidth = 16;
#if defined (PCM_OFFLOAD_ENABLED) || defined (PCM_OFFLOAD_ENABLED_24)
    format->findInt32(kKeySampleBits, &bitWidth);
    tempMetadata->setInt32(kKeySampleBits, bitWidth);
    tempMetadata->setInt32(kKeyPcmFormat, AUDIO_FORMAT_PCM_16_BIT);
    char prop_pcmoffload[PROPERTY_VALUE_MAX] = {0};
    property_get("audio.offload.pcm.24bit.enable", prop_pcmoffload, "0");
    if ((offloadAudio) &&
        (24 == bitWidth) &&
        (!strcmp(prop_pcmoffload, "true") || atoi(prop_pcmoffload))) {
        tempMetadata->setInt32(kKeyPcmFormat, AUDIO_FORMAT_PCM_8_24_BIT);
    }
#endif
    return tempMetadata;
}

/*
QCOM HW AAC encoder allowed bitrates
------------------------------------------------------------------------------------------------------------------
Bitrate limit |AAC-LC(Mono)           | AAC-LC(Stereo)        |AAC+(Mono)            | AAC+(Stereo)            | eAAC+                      |
Minimum     |Min(24000,0.5 * f_s)   |Min(24000,f_s)           | 24000                      |24000                        |  24000                       |
Maximum    |Min(192000,6 * f_s)    |Min(192000,12 * f_s)  | Min(192000,6 * f_s)  | Min(192000,12 * f_s)  |  Min(192000,12 * f_s) |
------------------------------------------------------------------------------------------------------------------
*/
bool ExtendedUtils::UseQCHWAACEncoder(audio_encoder Encoder,int32_t Channel,int32_t BitRate,int32_t SampleRate)
{
    int minBiteRate = -1;
    int maxBiteRate = -1;
    char propValue[PROPERTY_VALUE_MAX] = {0};
    bool currentState;
    ARG_TOUCH(BitRate);

    property_get("qcom.hw.aac.encoder",propValue,NULL);
    if (!strncmp(propValue,"true",sizeof("true"))) {
        //check for QCOM's HW AAC encoder only when qcom.aac.encoder =  true;
        ALOGV("qcom.aac.encoder enabled, check AAC encoder(%d) allowed bitrates",Encoder);

        if (Channel == 0 && BitRate == 0 && SampleRate == 0) {
            //this is a query call, simply reset and return state
            ALOGV("mIsQCHWAACEncoder:%d", mIsQCHWAACEncoder);
            currentState = mIsQCHWAACEncoder;
            mIsQCHWAACEncoder = false;
            return currentState;
        }

        switch (Encoder) {
        case AUDIO_ENCODER_AAC:// for AAC-LC format
            if (Channel == 1) {//mono
                minBiteRate = MIN_BITERATE_AAC<(SampleRate/2)?MIN_BITERATE_AAC:(SampleRate/2);
                maxBiteRate = MAX_BITERATE_AAC<(SampleRate*6)?MAX_BITERATE_AAC:(SampleRate*6);
            } else if (Channel == 2) {//stereo
                minBiteRate = MIN_BITERATE_AAC<SampleRate?MIN_BITERATE_AAC:SampleRate;
                maxBiteRate = MAX_BITERATE_AAC<(SampleRate*12)?MAX_BITERATE_AAC:(SampleRate*12);
            }
            break;
        case AUDIO_ENCODER_HE_AAC:// for AAC+ format
            // Do not use HW AAC encoder for HE AAC(AAC+) formats.
            mIsQCHWAACEncoder = false;
            break;
        default:
            ALOGV("encoder:%d not supported by QCOM HW AAC encoder",Encoder);

        }

        //return true only when 1. minBiteRate and maxBiteRate are updated(not -1) 2. minBiteRate <= SampleRate <= maxBiteRate
        if (BitRate >= minBiteRate && BitRate <= maxBiteRate) {
            mIsQCHWAACEncoder = true;
        }
    }

    return mIsQCHWAACEncoder;
}

bool ExtendedUtils::is24bitPCMOffloadEnabled() {
    char propPCMOfload[PROPERTY_VALUE_MAX] = {0};
    property_get("audio.offload.pcm.24bit.enable", propPCMOfload, "0");
    if (!strncmp(propPCMOfload, "true", 4) || atoi(propPCMOfload))
        return true;
    else
        return false;
}

bool ExtendedUtils::is16bitPCMOffloadEnabled() {
    char propPCMOfload[PROPERTY_VALUE_MAX] = {0};
    property_get("audio.offload.pcm.16bit.enable", propPCMOfload, "0");
    if (!strncmp(propPCMOfload, "true", 4) || atoi(propPCMOfload))
        return true;
    else
        return false;
}

bool ExtendedUtils::isTrackOffloadEnabled() {
    char propTrackOffload[PROPERTY_VALUE_MAX] = {0};

    //track offload will work only if 16 bit PCM offloading is enabled
    if (is16bitPCMOffloadEnabled()) {
        property_get("audio.offload.track.enabled", propTrackOffload, "0");
        if (!strncmp(propTrackOffload, "true", 4) || atoi(propTrackOffload))
            return true;
    }

    return false;
}

bool ExtendedUtils::isRAWFormat(const sp<MetaData> &meta) {
    const char *mime = {0};
    if (meta == NULL) {
        return false;
    }
    CHECK(meta->findCString(kKeyMIMEType, &mime));
    if (!strncasecmp(mime, MEDIA_MIMETYPE_AUDIO_RAW, 9))
        return true;
    else
        return false;
}

bool ExtendedUtils::isRAWFormat(const sp<AMessage> &format) {
    AString mime;
    if (format == NULL) {
        return false;
    }
    CHECK(format->findString("mime", &mime));
    if (!strncasecmp(mime.c_str(), MEDIA_MIMETYPE_AUDIO_RAW, 9))
        return true;
    else
        return false;
}

int32_t ExtendedUtils::getPcmSampleBits(const sp<MetaData> &meta) {
    int32_t bitWidth = 16;
    if (meta != NULL) {
        meta->findInt32(kKeySampleBits, &bitWidth);
    }
    return bitWidth;
}

int32_t ExtendedUtils::getPcmSampleBits(const sp<AMessage> &format) {
    int32_t bitWidth = 16;
    if (format != NULL) {
        format->findInt32("sbit", &bitWidth);
    }
    return bitWidth;
}

int32_t ExtendedUtils::getPCMFormat(const sp<MetaData> &meta) {
    int32_t pcmFormat = AUDIO_FORMAT_INVALID;
    if (meta != NULL) {
        meta->findInt32(kKeyPcmFormat, &pcmFormat);
    }
    return pcmFormat;
}

void ExtendedUtils::setKeyPCMFormat(const sp<MetaData> &meta, int32_t pcmFormat) {
    if (meta != NULL) {
        meta->setInt32(kKeyPcmFormat, pcmFormat);
    }
}

status_t ExtendedUtils::convertToSinkFormat(const sp<ABuffer> &buffer, sp<ABuffer> &newBuffer,
                                                 audio_format_t srcFormat, audio_format_t pcmFormat,
                                                 bool isOffload) {
    size_t bps = audio_bytes_per_sample(srcFormat);

    if (bps <= 0) {
        ALOGE("Invalid pcmformat %x given for conversion", srcFormat);
        return INVALID_OPERATION;
    }

    size_t frames = buffer->size() / bps;

    if (frames == 0) {
        ALOGE("zero sized buffer, nothing to convert");
        return BAD_VALUE;
    }

    ALOGV("convert %zu bytes (frames %d) of format %x",
          buffer->size(), frames, srcFormat);

    audio_format_t dstFormat;
    if (isOffload) {
        switch (pcmFormat) {
            case AUDIO_FORMAT_PCM_16_BIT_OFFLOAD:
                dstFormat = AUDIO_FORMAT_PCM_16_BIT;
                break;
            case AUDIO_FORMAT_PCM_24_BIT_OFFLOAD:
                if (srcFormat == AUDIO_FORMAT_PCM_32_BIT) {
                    ALOGV("No conversion needed for 32 bit");
                    newBuffer = buffer;
                    return OK;
                }
                if (srcFormat != AUDIO_FORMAT_PCM_24_BIT_PACKED &&
                    srcFormat != AUDIO_FORMAT_PCM_8_24_BIT) {
                        ALOGE("Invalid src format for 24 bit conversion");
                        return INVALID_OPERATION;
                }
                dstFormat = AUDIO_FORMAT_PCM_24_BIT_OFFLOAD;
                break;
            case AUDIO_FORMAT_DEFAULT:
                ALOGI("OffloadInfo not yet initialized, retry");
                return NO_INIT;
            default:
                ALOGE("Invalid offload format %x given for conversion",
                      pcmFormat);
                return INVALID_OPERATION;
        }
    } else {
        if (pcmFormat == AUDIO_FORMAT_DEFAULT) {
            return NO_INIT;
        }

        dstFormat = pcmFormat;
    }

    if (srcFormat == dstFormat) {
        ALOGV("same format");
        newBuffer = buffer;
        return OK;
    }

    size_t dstFrameSize = audio_bytes_per_sample(dstFormat);
    size_t dstBytes = frames * dstFrameSize;

    newBuffer = new ABuffer(dstBytes);

    memcpy_by_audio_format(newBuffer->data(), dstFormat,
                           buffer->data(), srcFormat, frames);

    ALOGV("convert to format %x newBuffer->size() %zu",
          dstFormat, newBuffer->size());

    // copy over some meta info
    int64_t timeUs = 0;
    buffer->meta()->findInt64("timeUs", &timeUs);
    newBuffer->meta()->setInt64("timeUs", timeUs);

    int32_t eos = false;
    buffer->meta()->findInt32("eos", &eos);
    newBuffer->meta()->setInt32("eos", eos);

    newBuffer->meta()->setInt32("pcm-format", (int32_t)dstFormat);
    return OK;
}

//- returns NULL if we dont really need a new extractor (or cannot),
//  valid extractor is returned otherwise
//- caller needs to check for NULL
//  ----------------------------------------
//  defaultExt - the existing extractor
//  source - file source
//  mime - container mime
//  ----------------------------------------
//  Note: defaultExt will be deleted in this function if the new parser is selected

sp<MediaExtractor> ExtendedUtils::MediaExtractor_CreateIfNeeded(sp<MediaExtractor> defaultExt,
                                                            const sp<DataSource> &source,
                                                            const char *mime) {
    bool bCheckExtendedExtractor = false;
    bool videoTrackFound         = false;
    bool audioTrackFound         = false;
    bool amrwbAudio              = false;
    bool hevcVideo               = false;
    bool dolbyAudio              = false;
    bool mpeg4Container          = false;
    bool aacAudioTrack           = false;
    int  numOfTrack              = 0;

    mpeg4Container = !strncasecmp(mime,
                                MEDIA_MIMETYPE_CONTAINER_MPEG4,
                                strlen(MEDIA_MIMETYPE_CONTAINER_MPEG4));

    if (defaultExt != NULL) {
        for (size_t trackItt = 0; trackItt < defaultExt->countTracks(); ++trackItt) {
            ++numOfTrack;
            sp<MetaData> meta = defaultExt->getTrackMetaData(trackItt);
            const char *_mime;
            CHECK(meta->findCString(kKeyMIMEType, &_mime));

            String8 mime = String8(_mime);

            const char * dolbyFormats[ ] = {
                MEDIA_MIMETYPE_AUDIO_AC3,
                MEDIA_MIMETYPE_AUDIO_EAC3,
#ifdef DOLBY_UDC
                MEDIA_MIMETYPE_AUDIO_EAC3_JOC,
#endif
            };

            if (!strncasecmp(mime.string(), "audio/", 6)) {
                audioTrackFound = true;

                amrwbAudio = !strncasecmp(mime.string(),
                                          MEDIA_MIMETYPE_AUDIO_AMR_WB,
                                          strlen(MEDIA_MIMETYPE_AUDIO_AMR_WB));

                aacAudioTrack = !strncasecmp(mime.string(),
                                          MEDIA_MIMETYPE_AUDIO_AAC,
                                          strlen(MEDIA_MIMETYPE_AUDIO_AAC));

                for (size_t i = 0; i < ARRAY_SIZE(dolbyFormats); i++) {
                    if (!strncasecmp(mime.string(), dolbyFormats[i], strlen(dolbyFormats[i]))) {
                        dolbyAudio = true;
                    }
                }

                if (amrwbAudio || dolbyAudio) {
                    break;
                }
            } else if (!strncasecmp(mime.string(), "video/", 6)) {
                videoTrackFound = true;
                if(!strncasecmp(mime.string(), "video/hevc", 10)) {
                    hevcVideo = true;
                }
            }
        }

        if (amrwbAudio || dolbyAudio) {
            bCheckExtendedExtractor = true;
        } else if (numOfTrack  == 0) {
            bCheckExtendedExtractor = true;
        } else if (numOfTrack == 1) {
            if ((videoTrackFound) ||
                (!videoTrackFound && !audioTrackFound) ||
                (audioTrackFound && mpeg4Container && aacAudioTrack)) {
                bCheckExtendedExtractor = true;
            }
        } else if (numOfTrack >= 2) {
            if (videoTrackFound && audioTrackFound) {
                if (amrwbAudio || hevcVideo ) {
                    bCheckExtendedExtractor = true;
                }
            } else {
                bCheckExtendedExtractor = true;
            }
        }
    } else {
        bCheckExtendedExtractor = true;
    }

    if (!bCheckExtendedExtractor) {
        ALOGD("extended extractor not needed, return default");
        return defaultExt;
    }

    //Create Extended Extractor only if default extractor is not selected
    ALOGD("Try creating ExtendedExtractor");
    sp<MediaExtractor>  retExtExtractor = ExtendedExtractor::Create(source, mime);

    if (retExtExtractor == NULL) {
        ALOGD("Couldn't create the extended extractor, return default one");
        return defaultExt;
    }

    if (defaultExt == NULL) {
        ALOGD("default extractor is NULL, return extended extractor");
        return retExtExtractor;
    }

    //bCheckExtendedExtractor is true which means default extractor was found
    //but we want to give preference to extended extractor based on certain
    //conditions.

    //needed to prevent a leak in case both extractors are valid
    //but we still dont want to use the extended one. we need
    //to delete the new one
    bool bUseDefaultExtractor = true;

    const char * extFormats[ ] = {
        MEDIA_MIMETYPE_AUDIO_AMR_WB_PLUS,
        MEDIA_MIMETYPE_VIDEO_HEVC,
        MEDIA_MIMETYPE_AUDIO_AC3,
        MEDIA_MIMETYPE_AUDIO_EAC3,
#ifdef DOLBY_UDC
        MEDIA_MIMETYPE_AUDIO_EAC3_JOC,
#endif
        MEDIA_MIMETYPE_AUDIO_AAC,
        MEDIA_MIMETYPE_AUDIO_ALAC,
    };

    for (size_t trackItt = 0; (trackItt < retExtExtractor->countTracks()); ++trackItt) {
        sp<MetaData> meta = retExtExtractor->getTrackMetaData(trackItt);
        const char *mime;
        bool success = meta->findCString(kKeyMIMEType, &mime);
        bool isExtFormat = false;
        for (size_t i = 0; i < ARRAY_SIZE(extFormats); i++) {
            if (!strncasecmp(mime, extFormats[i], strlen(extFormats[i]))) {
                isExtFormat = true;
                break;
            }
        }

        if ((success == true) && isExtFormat) {
            ALOGD("Discarding default extractor and using the extended one");
            bUseDefaultExtractor = false;
            break;
        }
    }

    if (bUseDefaultExtractor) {
        ALOGD("using default extractor inspite of having a new extractor");
        retExtExtractor.clear();
        return defaultExt;
    } else {
        defaultExt.clear();
        return retExtExtractor;
    }

}

bool ExtendedUtils::isAVCProfileSupported(int32_t  profile) {
   if (profile == OMX_VIDEO_AVCProfileMain ||
           profile == OMX_VIDEO_AVCProfileHigh ||
           profile == OMX_VIDEO_AVCProfileBaseline) {
      return true;
   } else {
      return false;
   }
}

void ExtendedUtils::updateNativeWindowBufferGeometry(ANativeWindow* anw,
        OMX_U32 width, OMX_U32 height, OMX_COLOR_FORMATTYPE colorFormat) {
    ARG_TOUCH(anw);
    ARG_TOUCH(width);
    ARG_TOUCH(height);
    ARG_TOUCH(colorFormat);

#if UPDATE_BUFFER_GEOMETRY_AVAILABLE
    if (anw != NULL) {
        ALOGI("Calling native window update buffer geometry [%lu x %lu]",
                width, height);
        status_t err = anw->perform(
                anw, NATIVE_WINDOW_UPDATE_BUFFERS_GEOMETRY,
                width, height, colorFormat);
        if (err != OK) {
            ALOGE("UPDATE_BUFFER_GEOMETRY failed %d", err);
        }
    }
#endif
}

bool ExtendedUtils::checkIsThumbNailMode(const uint32_t flags, char* componentName) {
    bool isInThumbnailMode = false;
    if ((flags & OMXCodec::kClientNeedsFramebuffer) && !strncmp(componentName, "OMX.qcom.", 9)) {
        isInThumbnailMode = true;
    }
    return isInThumbnailMode;
}

void ExtendedUtils::applyPreRotation(
        const CameraParameters& params, sp<MetaData> &meta) {

    // Camera pre-rotates video buffers. Width and Height of
    // of the image will be flipped if rotation is 90 or 270.
    // Encoder must be made aware of the flip in this case.
    const char *pRotation = params.get("video-rotation");
    int32_t preRotation = pRotation ? atoi(pRotation) : 0;
    bool flip = preRotation % 180;

    if (flip) {
        int32_t width = 0;
        int32_t height = 0;
        meta->findInt32(kKeyWidth, &width);
        meta->findInt32(kKeyHeight, &height);

        // width assigned to height is intentional
        meta->setInt32(kKeyWidth, height);
        meta->setInt32(kKeyStride, height);
        meta->setInt32(kKeyHeight, width);
        meta->setInt32(kKeySliceHeight, width);
    }
}

void ExtendedUtils::updateVideoTrackInfoFromESDS_MPEG4Video(sp<MetaData> meta) {
    const char* mime = NULL;
    if (meta != NULL && meta->findCString(kKeyMIMEType, &mime) &&
            mime && !strcasecmp(mime, MEDIA_MIMETYPE_VIDEO_MPEG4)) {
        uint32_t type;
        const void *data;
        size_t size;
        if (!meta->findData(kKeyESDS, &type, &data, &size) || !data) {
            ALOGW("ESDS atom is invalid");
            return;
        }

        if (checkDPFromCodecSpecificData((const uint8_t*) data, size)) {
            meta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_VIDEO_MPEG4_DP);
        }
    }
}

bool ExtendedUtils::checkDPFromCodecSpecificData(const uint8_t *data, size_t size) {
    bool retVal = false;
    size_t offset = 0, startCodeOffset = 0;
    bool isStartCode = false;
    const int kVolStartCode = 0x20;
    const char kStartCode[] = "\x00\x00\x01";
    // must contain at least 4 bytes for video_object_layer_start_code
    const size_t kMinCsdSize = 4;

    if (!data || (size < kMinCsdSize)) {
        ALOGV("Invalid CSD (expected at least %zu bytes)", kMinCsdSize);
        return retVal;
    }

    while (offset < size - 3) {
        if ((data[offset + 3] & 0xf0) == kVolStartCode) {
            if (!memcmp(&data[offset], kStartCode, 3)) {
                startCodeOffset = offset;
                isStartCode = true;
                break;
            }
        }

        offset++;
    }

    if (isStartCode) {
        retVal = checkDPFromVOLHeader((const uint8_t*) &data[startCodeOffset],
                (size - startCodeOffset));
    }

    return retVal;
}

bool ExtendedUtils::checkDPFromVOLHeader(const uint8_t *data, size_t size) {
    bool retVal = false;
    // must contain at least 4 bytes for video_object_layer_start_code + 9 bits of data
    const size_t kMinHeaderSize = 6;

    if (!data || (size < kMinHeaderSize)) {
        ALOGV("Invalid VOL header (expected at least %zu bytes)", kMinHeaderSize);
        return false;
    }

    ALOGV("Checking for MPEG4 DP bit");
    ABitReader br(&data[4], (size - 4));
    br.skipBits(1); // random_accessible_vol

    unsigned videoObjectTypeIndication = br.getBits(8);
    if (videoObjectTypeIndication == 0x12u) {
        ALOGW("checkDPFromVOLHeader: videoObjectTypeIndication:%u",
               videoObjectTypeIndication);
        return false;
    }

    unsigned videoObjectLayerVerid = 1;
    if (br.getBits(1)) {
        videoObjectLayerVerid = br.getBits(4);
        br.skipBits(3); // video_object_layer_priority
        ALOGV("checkDPFromVOLHeader: videoObjectLayerVerid:%u",
               videoObjectLayerVerid);
    }

    if (br.getBits(4) == 0x0f) { // aspect_ratio_info
        ALOGV("checkDPFromVOLHeader: extended PAR");
        br.skipBits(8); // par_width
        br.skipBits(8); // par_height
    }

    if (br.getBits(1)) { // vol_control_parameters
        br.skipBits(2);  // chroma_format
        br.skipBits(1);  // low_delay
        if (br.getBits(1)) { // vbv_parameters
            br.skipBits(15); // first_half_bit_rate
            br.skipBits(1);  // marker_bit
            br.skipBits(15); // latter_half_bit_rate
            br.skipBits(1);  // marker_bit
            br.skipBits(15); // first_half_vbv_buffer_size
            br.skipBits(1);  // marker_bit
            br.skipBits(3);  // latter_half_vbv_buffer_size
            br.skipBits(11); // first_half_vbv_occupancy
            br.skipBits(1);  // marker_bit
            br.skipBits(15); // latter_half_vbv_occupancy
            br.skipBits(1);  // marker_bit
        }
    }

    unsigned videoObjectLayerShape = br.getBits(2);
    if (videoObjectLayerShape != 0x00u /* rectangular */) {
        ALOGV("checkDPFromVOLHeader: videoObjectLayerShape:%x",
               videoObjectLayerShape);
        return false;
    }

    br.skipBits(1); // marker_bit
    unsigned vopTimeIncrementResolution = br.getBits(16);
    br.skipBits(1); // marker_bit
    if (br.getBits(1)) {  // fixed_vop_rate
        // range [0..vopTimeIncrementResolution)

        // vopTimeIncrementResolution
        // 2 => 0..1, 1 bit
        // 3 => 0..2, 2 bits
        // 4 => 0..3, 2 bits
        // 5 => 0..4, 3 bits
        // ...

        if (vopTimeIncrementResolution <= 0u) {
            return BAD_VALUE;
        }

        --vopTimeIncrementResolution;
        unsigned numBits = 0;
        while (vopTimeIncrementResolution > 0) {
            ++numBits;
            vopTimeIncrementResolution >>= 1;
        }

        br.skipBits(numBits);  // fixed_vop_time_increment
    }

    br.skipBits(1);  // marker_bit
    br.skipBits(13); // video_object_layer_width
    br.skipBits(1);  // marker_bit
    br.skipBits(13); // video_object_layer_height
    br.skipBits(1);  // marker_bit
    br.skipBits(1);  // interlaced
    br.skipBits(1);  // obmc_disable
    unsigned spriteEnable = 0;
    if (videoObjectLayerVerid == 1) {
        spriteEnable = br.getBits(1);
    } else {
        spriteEnable = br.getBits(2);
    }

    if (spriteEnable == 0x1) { // static
        int spriteWidth = br.getBits(13);
        ALOGV("checkDPFromVOLHeader: spriteWidth:%d", spriteWidth);
        br.skipBits(1) ; // marker_bit
        br.skipBits(13); // sprite_height
        br.skipBits(1);  // marker_bit
        br.skipBits(13); // sprite_left_coordinate
        br.skipBits(1);  // marker_bit
        br.skipBits(13); // sprite_top_coordinate
        br.skipBits(1);  // marker_bit
        br.skipBits(6);  // no_of_sprite_warping_points
        br.skipBits(2);  // sprite_warping_accuracy
        br.skipBits(1);  // sprite_brightness_change
        br.skipBits(1);  // low_latency_sprite_enable
    } else if (spriteEnable == 0x2) { // GMC
        br.skipBits(6); // no_of_sprite_warping_points
        br.skipBits(2); // sprite_warping_accuracy
        br.skipBits(1); // sprite_brightness_change
    }

    if (videoObjectLayerVerid != 1
            && videoObjectLayerShape != 0x0u) {
        br.skipBits(1);
    }

    if (br.getBits(1)) { // not_8_bit
        br.skipBits(4);  // quant_precision
        br.skipBits(4);  // bits_per_pixel
    }

    if (videoObjectLayerShape == 0x3) {
        br.skipBits(1);
        br.skipBits(1);
        br.skipBits(1);
    }

    if (br.getBits(1)) { // quant_type
        if (br.getBits(1)) { // load_intra_quant_mat
            unsigned IntraQuantMat = 1;
            for (int i = 0; i < 64 && IntraQuantMat; i++) {
                 IntraQuantMat = br.getBits(8);
            }
        }

        if (br.getBits(1)) { // load_non_intra_quant_matrix
            unsigned NonIntraQuantMat = 1;
            for (int i = 0; i < 64 && NonIntraQuantMat; i++) {
                 NonIntraQuantMat = br.getBits(8);
            }
        }
    } /* quantType */

    if (videoObjectLayerVerid != 1) {
        unsigned quarterSample = br.getBits(1);
        ALOGV("checkDPFromVOLHeader: quarterSample:%u",
                quarterSample);
    }

    br.skipBits(1); // complexity_estimation_disable
    br.skipBits(1); // resync_marker_disable
    unsigned dataPartitioned = br.getBits(1);
    if (dataPartitioned) {
        retVal = true;
    }

    ALOGD("checkDPFromVOLHeader: DP:%u", dataPartitioned);
    return retVal;
}

bool ExtendedUtils::RTSPStream::ParseURL_V6(
        AString *host, const char **colonPos) {

    ssize_t bracketEnd = host->find("]");
    ALOGI("ExtendedUtils::ParseURL_V6() : host->c_str() = %s", host->c_str());

    if (bracketEnd <= 0) {
        return false;
    }

    // If there is a port present, leave it for parsing in ParseURL
    // otherwise, remove all trailing characters in the hostname
    size_t trailing = host->size() - bracketEnd;
    if (host->find(":", bracketEnd) == bracketEnd + 1) {
        // 2 characters must be subtracted to account for the removal of
        // the starting and ending brackets below --> bracketEnd + 1 - 2
        *colonPos = host->c_str() + bracketEnd - 1;
        trailing = 1;
    }

    host->erase(bracketEnd, trailing);
    host->erase(0, 1);

    return true;
}

void ExtendedUtils::RTSPStream::MakePortPair_V6(
        int *rtpSocket, int *rtcpSocket, unsigned *rtpPort) {

    struct addrinfo hints, *result = NULL;

    ALOGV("ExtendedUtils::RTSPStream::MakePortPair_V6()");

    *rtpSocket = socket(AF_INET6, SOCK_DGRAM, 0);
    CHECK_GE(*rtpSocket, 0);
    bumpSocketBufferSize_V6(*rtpSocket);

    *rtcpSocket = socket(AF_INET6, SOCK_DGRAM, 0);
    CHECK_GE(*rtcpSocket, 0);

    bumpSocketBufferSize_V6(*rtcpSocket);

    /* rand() * 1000 may overflow int type, use long long */
    unsigned start = (unsigned)((rand()* 1000ll)/RAND_MAX) + 15550;
    start &= ~1;

     for (unsigned port = start; port < 65536; port += 2) {
         struct sockaddr_in6 addr;
         addr.sin6_family = AF_INET6;
         addr.sin6_addr = in6addr_any;
         addr.sin6_port = htons(port);

         if (bind(*rtpSocket,
                  (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
             continue;
         }

         addr.sin6_port = htons(port + 1);

         if (bind(*rtcpSocket,
                  (const struct sockaddr *)&addr, sizeof(addr)) == 0) {
             *rtpPort = port;
             ALOGV("END MakePortPair_V6: %u", port);
             return;
         }
    }
    TRESPASS();
}

void ExtendedUtils::RTSPStream::bumpSocketBufferSize_V6(int s) {
    int size = 256 * 1024;
    CHECK_EQ(setsockopt(s, IPPROTO_IPV6, IPV6_RECVPKTINFO, &size, sizeof(size)), 0);
}

bool ExtendedUtils::RTSPStream::pokeAHole_V6(int rtpSocket, int rtcpSocket,
                const AString &transport, AString &sessionHost) {
    struct sockaddr_in addr;
    memset(addr.sin_zero, 0, sizeof(addr.sin_zero));
    addr.sin_family = AF_INET6;

    struct addrinfo hints, *result = NULL;
    ALOGV("Inside ExtendedUtils::RTSPStream::pokeAHole_V6");
    memset(&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    AString source;
    AString server_port;

    Vector<struct sockaddr_in> s_addrs;
    if (GetAttribute(transport.c_str(), "source", &source)) {
        ALOGI("found 'source' = %s field in Transport response",
            source.c_str());
        int err = getaddrinfo(source.c_str(), NULL, &hints, &result);
        if (err != 0 || result == NULL) {
            ALOGI("no need to poke the hole");
        } else {
            s_addrs.push(*(struct sockaddr_in *)result->ai_addr);
        }
    }

    int err = getaddrinfo(sessionHost.c_str(), NULL, &hints, &result);
    if (err != 0 || result == NULL) {
        ALOGE("Failed to look up address of session host '%s' err:%d(%s)",
            sessionHost.c_str(), err, gai_strerror(err));

        return false;
     } else {
        ALOGD("get the endpoint address of session host");
        addr = (*(struct sockaddr_in *)result->ai_addr);

        if (addr.sin_addr.s_addr == INADDR_NONE || IN_LOOPBACK(ntohl(addr.sin_addr.s_addr))) {
            ALOGI("no need to poke the hole");
        } else if (s_addrs.size() == 0 || s_addrs[0].sin_addr.s_addr != addr.sin_addr.s_addr) {
            s_addrs.push(addr);
        }
    }

    if (s_addrs.size() == 0){
        ALOGW("Failed to get any session address");
        return false;
    }

    if (!GetAttribute(transport.c_str(),
                             "server_port",
                             &server_port)) {
        ALOGW("Missing 'server_port' field in Transport response.");
        return false;
    }

    int rtpPort, rtcpPort;
    if (sscanf(server_port.c_str(), "%d-%d", &rtpPort, &rtcpPort) != 2
            || rtpPort <= 0 || rtpPort > 65535
            || rtcpPort <=0 || rtcpPort > 65535
            || rtcpPort != rtpPort + 1) {
        ALOGE("Server picked invalid RTP/RTCP port pair %s,"
             " RTP port must be even, RTCP port must be one higher.",
             server_port.c_str());

        return false;
    }

    if (rtpPort & 1) {
        ALOGW("Server picked an odd RTP port, it should've picked an "
             "even one, we'll let it pass for now, but this may break "
             "in the future.");
    }

    // Make up an RR/SDES RTCP packet.
    sp<ABuffer> buf = new ABuffer(65536);
    buf->setRange(0, 0);
    addRR(buf);
    addSDES(rtpSocket, buf);

    for (uint32_t i = 0; i < s_addrs.size(); i++){
        addr.sin_addr.s_addr = s_addrs[i].sin_addr.s_addr;

        addr.sin_port = htons(rtpPort);

        ssize_t n = sendto(
                rtpSocket, buf->data(), buf->size(), 0,
                (const sockaddr *)&addr, sizeof(sockaddr_in6));

        if (n < (ssize_t)buf->size()) {
            ALOGE("failed to poke a hole for RTP packets");
            continue;
        }

        addr.sin_port = htons(rtcpPort);

        n = sendto(
                rtcpSocket, buf->data(), buf->size(), 0,
                (const sockaddr *)&addr, sizeof(sockaddr_in6));

        if (n < (ssize_t)buf->size()) {
            ALOGE("failed to poke a hole for RTCP packets");
            continue;
        }

        ALOGV("successfully poked holes for the address = %u", s_addrs[i].sin_addr.s_addr);
    }

    return true;
}

void ExtendedUtils::RTSPStream::notifyBye(const sp<AMessage> &msg, const int32_t what) {
    msg->setInt32("what", what);
    msg->post();
}

bool ExtendedUtils::RTSPStream::GetAttribute(const char *s, const char *key, AString *value) {
    value->clear();

    size_t keyLen = strlen(key);

    for (;;) {
        while (isspace(*s)) {
            ++s;
        }

        const char *colonPos = strchr(s, ';');

        size_t len =
            (colonPos == NULL) ? strlen(s) : colonPos - s;

        if (len >= keyLen + 1 && s[keyLen] == '=' && !strncmp(s, key, keyLen)) {
            value->setTo(&s[keyLen + 1], len - keyLen - 1);
            return true;
        }

        if (colonPos == NULL) {
            return false;
        }

        s = colonPos + 1;
    }
}

void ExtendedUtils::RTSPStream::addRR(const sp<ABuffer> &buf) {
    uint8_t *ptr = buf->data() + buf->size();
    ptr[0] = 0x80 | 0;
    ptr[1] = 201;  // RR
    ptr[2] = 0;
    ptr[3] = 1;
    ptr[4] = 0xde;  // SSRC
    ptr[5] = 0xad;
    ptr[6] = 0xbe;
    ptr[7] = 0xef;

    buf->setRange(0, buf->size() + 8);
}

void ExtendedUtils::RTSPStream::addSDES(int s, const sp<ABuffer> &buffer) {
    struct sockaddr_in addr;
    socklen_t addrSize = sizeof(addr);
    CHECK_EQ(0, getsockname(s, (sockaddr *)&addr, &addrSize));

    uint8_t *data = buffer->data() + buffer->size();
    data[0] = 0x80 | 1;
    data[1] = 202;  // SDES
    data[4] = 0xde;  // SSRC
    data[5] = 0xad;
    data[6] = 0xbe;
    data[7] = 0xef;

    size_t offset = 8;

    data[offset++] = 1;  // CNAME

    AString cname = "stagefright@";
    cname.append(inet_ntoa(addr.sin_addr));
    data[offset++] = cname.size();

    memcpy(&data[offset], cname.c_str(), cname.size());
    offset += cname.size();

    data[offset++] = 6;  // TOOL

    AString tool = MakeUserAgent();

    data[offset++] = tool.size();

    memcpy(&data[offset], tool.c_str(), tool.size());
    offset += tool.size();

    data[offset++] = 0;

    if ((offset % 4) > 0) {
        size_t count = 4 - (offset % 4);
        switch (count) {
            case 3:
                data[offset++] = 0;
            case 2:
                data[offset++] = 0;
            case 1:
                data[offset++] = 0;
        }
    }

    size_t numWords = (offset / 4) - 1;
    data[2] = numWords >> 8;
    data[3] = numWords & 0xff;

    buffer->setRange(buffer->offset(), buffer->size() + offset);
}

int32_t ExtendedUtils::getEncoderTypeFlags() {
    int32_t flags = 0;

    char mDeviceName[PROPERTY_VALUE_MAX];
    property_get("ro.board.platform", mDeviceName, "0");
    if (!strncmp(mDeviceName, "msm8909", 7)) {
        flags |= OMXCodec::kHardwareCodecsOnly;
    }

    return flags;
}

void ExtendedUtils::cacheCaptureBuffers(sp<ICamera> camera, video_encoder encoder) {
    if (camera != NULL) {
        char mDeviceName[PROPERTY_VALUE_MAX];
        property_get("ro.board.platform", mDeviceName, "0");
        if (!strncmp(mDeviceName, "msm8909", 7)) {
            int64_t token = IPCThreadState::self()->clearCallingIdentity();
            String8 s = camera->getParameters();
            CameraParameters params(s);
            const char *enable;
            if (encoder == VIDEO_ENCODER_H263 ||
                encoder == VIDEO_ENCODER_MPEG_4_SP) {
                enable = "1";
            } else {
                enable = "0";
            }
            params.set("cache-video-buffers", enable);
            if (camera->setParameters(params.flatten()) != OK) {
                ALOGE("Failed to enabled cached camera buffers");
            }
            IPCThreadState::self()->restoreCallingIdentity(token);
        }
    }
}
//return true if mime type is not support for pcm offload
//return true if PCM offload is not enabled
bool ExtendedUtils::pcmOffloadException(const char* const mime) {
    bool decision = false;

    if (!mime) {
        ALOGV("%s: no audio mime present, ignoring pcm offload", __func__);
        return true;
    }
#if defined (PCM_OFFLOAD_ENABLED) || defined (PCM_OFFLOAD_ENABLED_24)
    const char * const ExceptionTable[] = {
        MEDIA_MIMETYPE_AUDIO_AMR_NB,
        MEDIA_MIMETYPE_AUDIO_AMR_WB,
        MEDIA_MIMETYPE_AUDIO_QCELP,
        MEDIA_MIMETYPE_AUDIO_G711_ALAW,
        MEDIA_MIMETYPE_AUDIO_G711_MLAW,
        MEDIA_MIMETYPE_AUDIO_EVRC
    };
    int countException = (sizeof(ExceptionTable) / sizeof(ExceptionTable[0]));

    for(int i = 0; i < countException; i++) {
        if (!strcasecmp(mime, ExceptionTable[i])) {
            decision = true;
            break;
        }
    }
    ALOGI("decision %d mime %s", decision, mime);
    return decision;
#else
    //if PCM offload flag is disabled, do not offload any sessions
    //using pcm offload
    decision = true;
    ALOGI("decision %d mime %s", decision, mime);
    return decision;
#endif
}

sp<MetaData> ExtendedUtils::createPCMMetaFromSource(
                const sp<MetaData> &sMeta)
{
    sp<MetaData> tPCMMeta = new MetaData;
    //hard code as RAW
    tPCMMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_RAW);

    //TODO: remove this hard coding and use the meta info, but the issue
    //is that decoder does not provide this info for now
    tPCMMeta->setInt32(kKeySampleBits, 16);

    if (sMeta == NULL) {
        ALOGW("no meta returning dummy meta");
        return tPCMMeta;
    }

    int32_t srate = -1;
    if (!sMeta->findInt32(kKeySampleRate, &srate)) {
        ALOGV("No sample rate");
    }
    tPCMMeta->setInt32(kKeySampleRate, srate);

    int32_t cmask = 0;
    if (!sMeta->findInt32(kKeyChannelMask, &cmask) || (cmask == 0)) {
        ALOGI("No channel mask, try channel count");
    }
    int32_t channelCount = 0;
    if (!sMeta->findInt32(kKeyChannelCount, &channelCount)) {
        ALOGI("No channel count either");
    } else {
        //if channel mask is not set till now, use channel count
        //to retrieve channel count
        if (!cmask) {
            cmask = audio_channel_out_mask_from_count(channelCount);
        }
    }
    tPCMMeta->setInt32(kKeyChannelCount, channelCount);
    tPCMMeta->setInt32(kKeyChannelMask, cmask);

    int64_t duration = INT_MAX;
    if (!sMeta->findInt64(kKeyDuration, &duration)) {
        ALOGW("No duration in meta setting max duration");
    }
    tPCMMeta->setInt64(kKeyDuration, duration);

    int32_t bitRate = -1;
    if (!sMeta->findInt32(kKeyBitRate, &bitRate)) {
        ALOGW("No bitrate info");
    } else {
        tPCMMeta->setInt32(kKeyBitRate, bitRate);
    }

    return tPCMMeta;
}

void ExtendedUtils::overWriteAudioFormat(
                sp<AMessage> &dst, const sp<AMessage> &src)
{
    int32_t dchannels = 0;
    int32_t schannels = 0;
    int32_t drate = 0;
    int32_t srate = 0;
    int32_t dmask = 0;
    int32_t smask = 0;
    int32_t scmask = 0;
    int32_t dcmask = 0;

    dst->findInt32("channel-count", &dchannels);
    src->findInt32("channel-count", &schannels);

    dst->findInt32("sample-rate", &drate);
    src->findInt32("sample-rate", &srate);

    dst->findInt32("channel-mask", &dmask);
    src->findInt32("channel-mask", &smask);

    ALOGI("channel count src: %d dst: %d", schannels, dchannels);
    ALOGI("sample rate src: %d dst:%d ", srate, drate);

    scmask = audio_channel_count_from_out_mask(smask);
    dcmask = audio_channel_count_from_out_mask(dmask);
    ALOGI("channel mask src: %d dst:%d ", smask, dmask);
    ALOGI("channel count from mask src: %d dst:%d ", scmask, dcmask);

    if (schannels && dchannels != schannels) {
        dst->setInt32("channel-count", schannels);
    }

    if (srate && drate != srate) {
        dst->setInt32("sample-rate", srate);
    }

    if (dmask != smask) {
        dst->setInt32("channel-mask", smask);
    }

    return;
}

void ExtendedUtils::detectAndPostImage(const sp<ABuffer> accessUnit,
        const sp<AMessage> &notify) {
    if (accessUnit == NULL || notify == NULL)
        return;
    sp<RefBase> obj;
    if (accessUnit->meta()->findObject("format", &obj) && obj != NULL) {
        sp<MetaData> format = static_cast<MetaData*>(obj.get());
        const void* data;
        uint32_t type;
        size_t size;
        if (format->findData(kKeyAlbumArt, &type, &data, &size)) {
            ALOGV("found album image");
            sp<ABuffer> imagebuffer = ABuffer::CreateAsCopy(data, size);
            notify->setBuffer("image-buffer", imagebuffer);
            notify->post();
            format->remove(kKeyAlbumArt);
        }
    }
}

void ExtendedUtils::showImageInNativeWindow(const sp<AMessage> &msg,
        const sp<AMessage> &format) {
    if (msg == NULL || format == NULL)
        return;

    sp<ABuffer> buffer;
    if (!msg->findBuffer("image-buffer", &buffer) || buffer == NULL)
        return;

    sp<RefBase> obj;
    if (!msg->findObject("native-window", &obj) || obj == NULL)
        return;

    sp<ANativeWindow> nativeWindow = (static_cast<NativeWindowWrapper *>(obj.get()))->getNativeWindow();

    ALOGV("decode jpeg to rgb565");
    jpeg_decompress_struct cinfo;
    jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, buffer->data(), buffer->size());

    if (JPEG_HEADER_OK != jpeg_read_header(&cinfo, true)) {
        ALOGE("failed to decode jpeg header");
        jpeg_destroy_decompress(&cinfo);
        return;
    }

    cinfo.out_color_space = JCS_RGB_565;
    if (!jpeg_start_decompress(&cinfo)) {
        ALOGE("failed to decompress jpeg picture");
        jpeg_destroy_decompress(&cinfo);
        return;
    }

    ALOGV("Picture width = %d, height = %d", cinfo.output_width, cinfo.output_height);
    size_t stride = cinfo.output_width * 2;
    size_t dataSize = stride * cinfo.output_height;
    sp<ABuffer> outBuffer = new ABuffer(dataSize);
    for (size_t i = 0; i < cinfo.output_height; i++) {
        JSAMPLE* rowptr = (JSAMPLE*)(outBuffer->data() + stride * i);
        int32_t row_count = jpeg_read_scanlines(&cinfo, &rowptr, 1);
        if (row_count == 0) {
           ALOGV("row_count = 0");
           cinfo.output_scanline = cinfo.output_height;
           break;
        }
    }
    size_t bufwidth = cinfo.output_width;
    size_t bufheight = cinfo.output_height;
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    ALOGV("finish decoding jpeg");

    int32_t err = 0;

    err = native_window_set_usage(nativeWindow.get(),
            GRALLOC_USAGE_SW_READ_NEVER | GRALLOC_USAGE_SW_WRITE_OFTEN
            | GRALLOC_USAGE_HW_TEXTURE | GRALLOC_USAGE_EXTERNAL_DISP);
    if (err != 0) {
        ALOGE("native_window_set_usage failed: %d", err);
        return;
    }
    err = native_window_set_scaling_mode(nativeWindow.get(),
            NATIVE_WINDOW_SCALING_MODE_SCALE_TO_WINDOW);
    if (err != 0) {
        ALOGE("native_window_set_scaling_mode failed: %d", err);
        return;
    }
    err = native_window_set_buffers_dimensions(nativeWindow.get(), bufwidth,
            bufheight);
    if (err != 0) {
        ALOGE("native_window_set_buffers_dimensions failed: %d", err);
        return;
    }
    err = native_window_set_buffers_format(nativeWindow.get(),
            HAL_PIXEL_FORMAT_RGB_565);
    if (err != 0) {
        ALOGE("native_window_set_buffers_format failed: %d", err);
        return;
    }

    android_native_rect_t crop;
    crop.left = 0;
    crop.top = 0;
    crop.right = bufwidth - 1;
    crop.bottom = bufheight - 1;

    err = native_window_set_crop(nativeWindow.get(), &crop);
    if (err != 0) {
        ALOGE("native_window_set_crop failed: %ld", err);
        return;
    }

    err = native_window_set_buffers_transform(
            nativeWindow.get(), 0);
    if (err != 0) {
        ALOGE("native_window_set_buffers_transform failed: %ld", err);
        return;
    }

    ANativeWindowBuffer *buf;
    if ((err = native_window_dequeue_buffer_and_wait(nativeWindow.get(),
            &buf)) != 0) {
        ALOGE("native_window_dequeue_buffer_and_wait returned error %d", err);
        buf = NULL;
        return;
    }
    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
    Rect bounds(bufwidth, bufheight);

    void *dst;
    if ((err = mapper.lock(buf->handle, GRALLOC_USAGE_SW_WRITE_OFTEN,
            bounds, &dst)) != 0) {
        ALOGE("mapper.lock failed %d", err);
        buf = NULL;
        return;
    }

    memcpy((uint8_t*)dst, outBuffer->data(), dataSize);

    if ((err = mapper.unlock(buf->handle)) != 0) {
        ALOGE("mapper.unlock failed %d", err);
        buf = NULL;
        return;
    }
    if ((err = nativeWindow->queueBuffer(nativeWindow.get(), buf,
            -1)) != 0) {
        ALOGE("native window queueBuffer returned error %d", err);
        buf = NULL;
        return;
    }
    buf = NULL;
    ALOGV("show the image in native window");
    format->setInt32("width", (int32_t)bufwidth);
    format->setInt32("height", (int32_t)bufheight);
}

bool ExtendedUtils::is24bitPCMOffloaded(const sp<MetaData> &sMeta) {
    bool decision = false;

    if (sMeta == NULL) {
        return decision;
    }

   /* Return true, if
      1. 24 bit offload flag is enabled
      2. the bit stream is raw
      3. this is 24 bit PCM */

    if (is24bitPCMOffloadEnabled() && isRAWFormat(sMeta) &&
        (getPcmSampleBits(sMeta) == 24 || getPcmSampleBits(sMeta) == 32)) {
        ALOGV("%s: decided its true for 24 bit PCM offloading", __func__);
        decision = true;
    }

    return decision;
}

bool ExtendedUtils::isVorbisFormat(const sp<MetaData> &meta) {
    const char *mime;

    if ((meta == NULL) || !(meta->findCString(kKeyMIMEType, &mime))) {
        return false;
    }

    return (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_VORBIS)) ? true : false;
}

size_t ExtendedUtils::getVorbisHdrSize(const sp<MetaData> &meta) {
    size_t vorbisHdrSize = 0;
    const void *hdrDat;
    size_t hdrSize;
    uint32_t type;

    if ((meta != NULL) && meta->findData(kKeyVorbisInfo, &type, &hdrDat, &hdrSize)) {
        vorbisHdrSize += hdrSize;
    }
    if ((meta != NULL) && meta->findData(kKeyVorbisData, &type, &hdrDat, &hdrSize)) {
        vorbisHdrSize += hdrSize;
    }
    if ((meta != NULL) && meta->findData(kKeyVorbisBooks, &type, &hdrDat, &hdrSize)) {
        vorbisHdrSize += hdrSize;
    }

    return vorbisHdrSize;
}

sp<ABuffer> ExtendedUtils::assembleVorbisHdr(const sp<MetaData> &meta) {
    size_t vorbisHdrSize = 0;
    sp<ABuffer> vorbisHdrBuffer = NULL;
    const size_t MAX_META_SIZE = 32 * 1024;
    const void *hdrDat1, *hdrDat2, *hdrDat3;
    size_t hdrSize1 = 0, hdrSize2 = 0, hdrSize3 = 0;
    uint32_t type;

    if (meta == NULL) {
        ALOGE("invalid meta data");
        return NULL;
    }

    if (meta->findData(kKeyVorbisInfo, &type, &hdrDat1, &hdrSize1)) {
        vorbisHdrSize += hdrSize1;
        vorbisHdrSize += 4;
    }

    if (meta->findData(kKeyVorbisData, &type, &hdrDat2, &hdrSize2)) {
        // comment packet size more than 32K will be zeroed,
        // dsp tolerates the missing of 2nd packet.
        if (hdrSize2 > MAX_META_SIZE) {
            hdrSize2 = 0;
        }
        vorbisHdrSize += hdrSize2;
        vorbisHdrSize += 4;
    } else {
        // fill vacant header even if parser doesn't provide kKeyVorbisData.
        hdrSize2 = 0;
        vorbisHdrSize += 4;
    }

    if (meta->findData(kKeyVorbisBooks, &type, &hdrDat3, &hdrSize3)) {
        vorbisHdrSize += hdrSize3;
        vorbisHdrSize += 4;
    }

    // assemble vorbis header
    if (vorbisHdrSize > 0) {
        size_t offset = 0;
        vorbisHdrBuffer = new ABuffer(vorbisHdrSize);
        vorbisHdrBuffer->setRange(0, 0);

        if (hdrSize1 > 0) {
            memcpy(vorbisHdrBuffer->base(), (int32_t *)&hdrSize1, sizeof(int32_t));
            memcpy(vorbisHdrBuffer->base() + sizeof(int32_t), hdrDat1, hdrSize1);
            offset += (hdrSize1 + sizeof(int32_t));
        }
        if (hdrSize2 >= 0) {
            memcpy(vorbisHdrBuffer->base() + offset, (int32_t *)&hdrSize2, sizeof(int32_t));
            memcpy(vorbisHdrBuffer->base() + offset + sizeof(int32_t), hdrDat2, hdrSize2);
            offset += (hdrSize2 + sizeof(int32_t));
        }
        if (hdrSize3 > 0) {
            memcpy(vorbisHdrBuffer->base() + offset, (int32_t *)&hdrSize3, sizeof(int32_t));
            memcpy(vorbisHdrBuffer->base() + offset + sizeof(int32_t), hdrDat3, hdrSize3);
            offset += (hdrSize3 + sizeof(int32_t));
        }
        vorbisHdrBuffer->setRange(0, offset);
    }

    return vorbisHdrBuffer;
}

bool ExtendedUtils::isWMAFormat(const sp<MetaData> &meta) {
    const char *mime;

    if ((meta == NULL) || !(meta->findCString(kKeyMIMEType, &mime))) {
        return false;
    }

    return (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA)) ? true : false;

}

bool ExtendedUtils::isAudioWMAPro(const sp<AMessage> &format) {
    AString mime;
    int32_t wmaVersion = kTypeWMA;

    if (format == NULL) {
        return false;
    }
    CHECK(format->findString("mime", &mime));
    if (!strncasecmp(mime.c_str(), MEDIA_MIMETYPE_AUDIO_WMA, 14)) {
        format->findInt32("wmav", &wmaVersion);
    }

    return ((wmaVersion == kTypeWMAPro) || (wmaVersion == kTypeWMALossLess));
}

status_t ExtendedUtils::getWMAVersion(const sp<MetaData> &meta, int32_t *version) {
    const char *mime = NULL;
    int32_t wmaVersion = kTypeWMA;

    if(isWMAFormat(meta)) {
        if (meta->findInt32(kKeyWMAVersion, &wmaVersion)) {
            *version = wmaVersion;
            return OK;
        }
    }

    return BAD_VALUE;
}

status_t ExtendedUtils::sendMetaDataToHal(const sp<MetaData>& meta, AudioParameter *param) {

    const char *mime;
    bool success = meta->findCString(kKeyMIMEType, &mime);
    CHECK(success);

#ifdef FLAC_OFFLOAD_ENABLED
    int32_t minBlkSize = 0, maxBlkSize = 0, minFrmSize = 0, maxFrmSize = 0;
    if(!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_FLAC)) {
        if (meta->findInt32(kKeyMinBlkSize, &minBlkSize)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_FLAC_MIN_BLK_SIZE), minBlkSize);
        }
        if (meta->findInt32(kKeyMaxBlkSize, &maxBlkSize)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_FLAC_MAX_BLK_SIZE), maxBlkSize);
        }
        if (meta->findInt32(kKeyMinFrmSize, &minFrmSize)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_FLAC_MIN_FRAME_SIZE), minFrmSize);
        }
        if (meta->findInt32(kKeyMaxFrmSize, &maxFrmSize)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_FLAC_MAX_FRAME_SIZE), maxFrmSize);
        }
        ALOGV("FLAC metadata: minBlkSize %d, maxBlkSize %d, minFrmSize %d, maxFrmSize %d",
                minBlkSize, maxBlkSize, minFrmSize, maxFrmSize);
        return OK;
    }
#endif

#ifdef WMA_OFFLOAD_ENABLED
    int32_t wmaFormatTag = 0, wmaBlockAlign = 0, wmaChannelMask = 0;
    int32_t wmaBitsPerSample = 0, wmaEncodeOpt = 0, wmaEncodeOpt1 = 0;
    int32_t wmaEncodeOpt2 = 0;

    if(!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA)) {
        if (meta->findInt32(kKeyWMAFormatTag, &wmaFormatTag)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_FORMAT_TAG), wmaFormatTag);
        }
        if (meta->findInt32(kKeyWMABlockAlign, &wmaBlockAlign)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_BLOCK_ALIGN), wmaBlockAlign);
        }
        if (meta->findInt32(kKeyWMABitspersample, &wmaBitsPerSample)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_BIT_PER_SAMPLE), wmaBitsPerSample);
        }
        if (meta->findInt32(kKeyWMAChannelMask, &wmaChannelMask)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_CHANNEL_MASK), wmaChannelMask);
        }
        if (meta->findInt32(kKeyWMAEncodeOpt, &wmaEncodeOpt)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION), wmaEncodeOpt);
        }
        if (meta->findInt32(kKeyWMAAdvEncOpt1, &wmaEncodeOpt1)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION1), wmaEncodeOpt1);
        }
        if (meta->findInt32(kKeyWMAAdvEncOpt2, &wmaEncodeOpt2)) {
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_WMA_ENCODE_OPTION2), wmaEncodeOpt2);
        }
        ALOGV("WMA specific meta: fmt_tag 0x%x, blk_align %d, bits_per_sample %d, "
                "enc_options 0x%x", wmaFormatTag, wmaBlockAlign,
                wmaBitsPerSample, wmaEncodeOpt);
        return OK;
    }
#endif

    const void *data;
    size_t size;
    uint32_t type = 0;
    if (meta->findData(kKeyRawCodecSpecificData, &type, &data, &size)) {
        CHECK(data && (size == ALAC_CSD_SIZE || size == APE_CSD_SIZE));
        ALOGV("Found kKeyRawCodecSpecificData of size %d", size);
        const uint8_t *ptr = (uint8_t *) data;

#ifdef ALAC_OFFLOAD_ENABLED
        if (!strncasecmp(mime, MEDIA_MIMETYPE_AUDIO_ALAC,
                    strlen(MEDIA_MIMETYPE_AUDIO_ALAC))) {
            uint32_t frameLength = 0, maxFrameBytes = 0, avgBitRate = 0;
            uint32_t samplingRate = 0, channelLayoutTag = 0;
            uint8_t compatibleVersion = 0, pb = 0, mb = 0, kb = 0, numChannels = 0, bitDepth = 0;
            uint16_t maxRun = 0;

            memcpy(&frameLength, ptr + kKeyIndexAlacFrameLength, sizeof(frameLength));
            memcpy(&compatibleVersion, ptr + kKeyIndexAlacCompatibleVersion, sizeof(compatibleVersion));
            memcpy(&bitDepth, ptr + kKeyIndexAlacBitDepth, sizeof(bitDepth));
            memcpy(&pb, ptr + kKeyIndexAlacPb, sizeof(pb));
            memcpy(&mb, ptr + kKeyIndexAlacMb, sizeof(mb));
            memcpy(&kb, ptr + kKeyIndexAlacKb, sizeof(kb));
            memcpy(&numChannels, ptr + kKeyIndexAlacNumChannels, sizeof(numChannels));
            memcpy(&maxRun, ptr + kKeyIndexAlacMaxRun, sizeof(maxRun));
            memcpy(&maxFrameBytes, ptr + kKeyIndexAlacMaxFrameBytes, sizeof(maxFrameBytes));
            memcpy(&avgBitRate, ptr + kKeyIndexAlacAvgBitRate, sizeof(avgBitRate));
            memcpy(&samplingRate, ptr + kKeyIndexAlacSamplingRate, sizeof(samplingRate));

            ALOGV("ALAC CSD values: frameLength %d bitDepth %d numChannels %d"
                    " maxFrameBytes %d avgBitRate %d samplingRate %d",
                    frameLength, bitDepth, numChannels, maxFrameBytes, avgBitRate, samplingRate);

            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_FRAME_LENGTH), frameLength);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_COMPATIBLE_VERSION), compatibleVersion);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_BIT_DEPTH), bitDepth);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_PB), pb);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_MB), mb);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_KB), kb);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_NUM_CHANNELS), numChannels);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_MAX_RUN), maxRun);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_MAX_FRAME_BYTES), maxFrameBytes);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_AVG_BIT_RATE), avgBitRate);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_SAMPLING_RATE), samplingRate);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_ALAC_CHANNEL_LAYOUT_TAG), channelLayoutTag);
            return OK;
        }
#endif
#ifdef APE_OFFLOAD_ENABLED
        if (!strncasecmp(mime, MEDIA_MIMETYPE_AUDIO_APE,
                    strlen(MEDIA_MIMETYPE_AUDIO_APE))) {
            uint16_t compatibleVersion = 0, compressionLevel = 0;
            uint16_t bitsPerSample = 0, numChannels = 0;
            uint32_t formatFlags = 0, blocksPerFrame = 0, finalFrameBlocks = 0;
            uint32_t totalFrames = 0, sampleRate = 0, seekTablePresent = 0;

            memcpy(&compatibleVersion, ptr + kKeyIndexApeCompatibleVersion, sizeof(compatibleVersion));
            memcpy(&compressionLevel, ptr + kKeyIndexApeCompressionLevel, sizeof(compressionLevel));
            memcpy(&formatFlags, ptr + kKeyIndexApeFormatFlags, sizeof(formatFlags));
            memcpy(&blocksPerFrame, ptr + kKeyIndexApeBlocksPerFrame, sizeof(blocksPerFrame));
            memcpy(&finalFrameBlocks, ptr + kKeyIndexApeFinalFrameBlocks, sizeof(finalFrameBlocks));
            memcpy(&totalFrames, ptr + kKeyIndexApeTotalFrames, sizeof(totalFrames));
            memcpy(&bitsPerSample, ptr + kKeyIndexApeBitsPerSample, sizeof(bitsPerSample));
            memcpy(&numChannels, ptr + kKeyIndexApeNumChannels, sizeof(numChannels));
            memcpy(&sampleRate, ptr + kKeyIndexApeSampleRate, sizeof(sampleRate));
            memcpy(&seekTablePresent, ptr + kKeyIndexApeSeekTablePresent, sizeof(seekTablePresent));

            ALOGV("APE CSD values: compatibleVersion %d compressionLevel %d formatFlags %d"
                    " blocksPerFrame %d finalFrameBlocks %d totalFrames %d bitsPerSample %d"
                    " numChannels %d sampleRate %d seekTablePresent %d",
                    compatibleVersion, compressionLevel, formatFlags, blocksPerFrame, finalFrameBlocks, totalFrames,
                    bitsPerSample, numChannels, sampleRate, seekTablePresent);

            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_COMPATIBLE_VERSION), compatibleVersion);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_COMPRESSION_LEVEL), compressionLevel);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_FORMAT_FLAGS), formatFlags);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_BLOCKS_PER_FRAME), blocksPerFrame);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_FINAL_FRAME_BLOCKS), finalFrameBlocks);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_TOTAL_FRAMES), totalFrames);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_BITS_PER_SAMPLE), bitsPerSample);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_NUM_CHANNELS), numChannels);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_SAMPLE_RATE), sampleRate);
            param->addInt(String8(AUDIO_OFFLOAD_CODEC_APE_SEEK_TABLE_PRESENT), seekTablePresent);
            return OK;
        }
#endif
    }
    return OK;
}

#ifdef DTS_CODEC_M_
// Parse DTS header assuming the current ptr is start position of syncframe,
// update metadata only applicable, and return the payload size
size_t ExtendedUtils::DTS::parseDTSSyncFrame(
        const uint8_t *ptr, size_t size, sp<MetaData> *metaData) {

    #define DTS_SYNCWORD_CORE       0x7FFE8001
    #define DTS_SYNCWORD_SUBSTREAM  0x64582025
    #define DTS_SYNCWORD_LBR        0x0a801921

    ABitReader bits(ptr, size);
    unsigned syncValue = 0;
    static unsigned dtsChannelCount = 0;
    static unsigned dtsSamplingRate = 0;
    unsigned payloadSize = 0;

    // Check for DTS core or substream sync
    if (bits.numBitsLeft() < 32) {
        return 0;
    }

    // Get the sync value
    syncValue = bits.getBits(32);
    if (!((syncValue == DTS_SYNCWORD_CORE) ||
        (syncValue == DTS_SYNCWORD_SUBSTREAM))) {
        return 0;
    }

    if (syncValue == DTS_SYNCWORD_CORE) {
        // Stream with a core
        if (bits.numBitsLeft() < 1 + 5 + 1 + 7 + 14 + 6 + 4) { //FTYPE(1) + SHORT(5) + CRC(1) + NBLKS(7) + FSIZE(14) + AMODE(6) + SFREQ(4)
            ALOGV("Not enough bits left for further parsing");
            return 0;
        }

        bits.skipBits(14);  //FTYPE(1) + SHORT(5) + CRC(1) + NBLKS(7)

        // Get frame size
        unsigned frameByteSize = bits.getBits(14) + 1;
        payloadSize = frameByteSize;
        ALOGV("DTSHD core sub stream frame size = %u", frameByteSize);

        // Get channel count
        static const unsigned dtsChannelCountTable[] = { 1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8 };
        unsigned aMode = bits.getBits(6);
        dtsChannelCount = (aMode < 16) ? dtsChannelCountTable[aMode] : 0;

        // Get sampling freq
        static const unsigned samplingRateTable[] = {   0, 8000, 16000, 32000, 0,
                                                        0, 11025, 22050, 44100, 0,
                                                        0, 12000, 24000, 48000, 0, 0};
        unsigned srIndex = bits.getBits(4);
        dtsSamplingRate = samplingRateTable[srIndex];

    } else {
        // Stream with extension substream
        if (bits.numBitsLeft() < 2 + 2 + 1) { //UserDefinedBits(2) + nExtSSIndex(2) + bHeaderSizeType(1)
            ALOGV("Not enough bits left for further parsing DTSHD");
            return 0;
        }

        bits.skipBits(4); //UserDefinedBits(2) + nExtSSIndex(2)

        unsigned bHeaderSizeType = bits.getBits(1);
        unsigned numBits4Header = 0;
        unsigned numBits4ExSSFsize = 0;

        if (bHeaderSizeType == 0) {
            numBits4Header = 8;
            numBits4ExSSFsize = 16;
        } else {
            numBits4Header = 12;
            numBits4ExSSFsize = 20;
        }

        if (bits.numBitsLeft() < numBits4Header + numBits4ExSSFsize) {
            ALOGV("Not enough bits left for further parsing");
            return 0;
        }

        // Get substream header size
        unsigned numExtSSHeaderSize = bits.getBits(numBits4Header) + 1;

        // Get substream frame size
        unsigned numExtSSFsize = bits.getBits(numBits4ExSSFsize) + 1;
        unsigned frameByteSize = numExtSSFsize;
        payloadSize = frameByteSize;
        ALOGV("DTSHD extension sub stream info: frame size = %u", frameByteSize);

        // Scroll through to find the LBR header
        // Skip to nearest DWORD
        if (bHeaderSizeType == 0) {
            // 4 + 1 + 8 + 16 = 29
            bits.skipBits(3); //32-29
        }
        else {
            // 4 + 1 + 12 + 20 = 37
            bits.skipBits(27); //64-37
        }

        while (bits.numBitsLeft() >= 32) {
            // Big endian DWORD aligned sync
            if (bits.getBits(32) == DTS_SYNCWORD_LBR) {
                ALOGV("DTSHD extension sub stream info: LBR coding component syncword found");

                if (bits.numBitsLeft() < (8 + 8 + 16)) {
                    ALOGV("Not enough bits left for further lbr stream parsing");
                    return 0;
                }

                unsigned lbrHdrType = bits.getBits(8);
                if (lbrHdrType == 2) {
                    ALOGV("DTSHD extension sub stream info: LBR decoder init header code found");

                    unsigned lbrSampleRateCode = bits.getBits(8);
                    uint16_t lbrChannelMask = bits.getBits(16);

                    static const unsigned lbrSamplingRateTable[] = {    8000, 16000, 32000, 0, 0,
                                                                        22050, 44100, 0, 0, 0,
                                                                        12000, 24000, 48000, 0, 0, 0};
                    dtsSamplingRate = lbrSamplingRateTable[lbrSampleRateCode];

                    static const unsigned lbrChannelCountTable[] = {    1, 2, 2, 1,
                                                                        1, 2, 2,
                                                                        1, 1, 2,
                                                                        2, 2, 1,
                                                                        2, 1, 2};

                    // Spkr mask in big endian format
                    lbrChannelMask = ((lbrChannelMask >> 8) | (lbrChannelMask << 8));
                    ALOGV("DTSHD extension sub stream info: lbrChannelMask=%04x", (unsigned)lbrChannelMask);
                    dtsChannelCount = 0;
                    for (unsigned i = 0; i < 16; i++) {
                        if (lbrChannelMask & 1) {
                            dtsChannelCount += lbrChannelCountTable[i];
                        }
                        lbrChannelMask >>= 1;
                    }
                    ALOGV("DTSHD extension sub stream info: dtsSamplingRate=%u, dtsChannelCount=%u",
                            dtsSamplingRate, dtsChannelCount);
                    break;
                }
            }
        }
    }

    if (metaData != NULL) {
        (*metaData)->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_DTS);
        (*metaData)->setInt32(kKeyChannelCount, dtsChannelCount);
        (*metaData)->setInt32(kKeySampleRate, dtsSamplingRate);
    }

    return payloadSize;
}

bool ExtendedUtils::DTS::IsSeeminglyValidDTSHeader(const uint8_t *ptr, size_t size) {
    return parseDTSSyncFrame(ptr, size, NULL) > 0;
}
#endif //DTS_CODEC_M_

bool ExtendedUtils::isALACFormat(const sp<MetaData> &meta) {
    const char *mime;

    if ((meta == NULL) || !(meta->findCString(kKeyMIMEType, &mime))) {
        return false;
    }

    return (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_ALAC)) ? true : false;

}

bool ExtendedUtils::isAPEFormat(const sp<MetaData> &meta) {
    const char *mime;

    if ((meta == NULL) || !(meta->findCString(kKeyMIMEType, &mime))) {
        return false;
    }

    return (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_APE)) ? true : false;

}

bool ExtendedUtils::checkAPECompressionLevel(const sp<MetaData> &meta) {
    const void *data;
    size_t size;
    uint32_t type = 0, compressionLevel = 0;

    if (meta != NULL && meta->findData(kKeyRawCodecSpecificData, &type, &data, &size)) {
        CHECK(data && (size == APE_CSD_SIZE));
        memcpy(&compressionLevel, (uint8_t *) data + kKeyIndexApeCompressionLevel, sizeof(compressionLevel));
    }
    if (compressionLevel != APE_COMPRESSION_LEVEL_FAST &&
        compressionLevel != APE_COMPRESSION_LEVEL_NORMAL &&
        compressionLevel != APE_COMPRESSION_LEVEL_HIGH) {
        ALOGD("Disallow offload for APE clip with compressionLevel %d", compressionLevel);
        return false;
    }
    return true;
}

bool ExtendedUtils::isHwAudioDecoderSessionAllowed(const char *mime) {

    if (mime != NULL) {
        int isallowed = 0;
        String8  hal_result,hal_query;

        hal_query = AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED;
        //Append mime type to query
        hal_query += "=";
        hal_query +=  mime;

        //Check with HAL whether new session can be created for DSP only decoding formats
        hal_result = AudioSystem::getParameters((audio_io_handle_t)0, hal_query);
        AudioParameter result = AudioParameter(hal_result);
        if (result.getInt(String8(AUDIO_PARAMETER_IS_HW_DECODER_SESSION_ALLOWED),
                                     isallowed) == NO_ERROR) {
            if(0 == isallowed) {
               ALOGD(" ExtendedUtils : Rejecting audio decoder session request for %s ", mime);
               return false;
            }
        }
    }
    return true;
}

sp<MediaCodec> ExtendedUtils::CreateCustomComponentByName(const sp<ALooper> &looper,
                        const char* mime, bool encoder) {
    sp<MediaCodec> codec = NULL;

    if (mime != NULL) {
        ALOGV("createByComponentName for clip of mimetype %s", mime);
        if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_ALAC) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.alac");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_APE) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.ape");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.wma");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA_PRO) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.wma10Pro");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_WMA_LOSSLESS) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.wmaLossLess");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB_PLUS) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.amrwbplus");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_QCELP) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.Qcelp13");
        } else if (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_EVRC) && !encoder) {
            codec = MediaCodec::CreateByComponentName(looper, "OMX.qcom.audio.decoder.evrc");
        } else {
            ALOGV("Could not create by component name");
        }
    }

    return codec;
}

void ExtendedUtils::extractBitWidth(const sp<AMessage> &format,
                        audio_format_t audioFormat, int32_t *bitWidth) {
    int32_t bitsPerSample;
    if ((audioFormat == AUDIO_FORMAT_FLAC) &&
        format->findInt32("bits-per-sample", &bitsPerSample)) {
        CHECK(bitsPerSample == 16 || bitsPerSample == 24);
        *bitWidth = bitsPerSample;
    } else if (audioFormat == AUDIO_FORMAT_VORBIS) {
        *bitWidth = 16;

        char value[PROPERTY_VALUE_MAX];
        if (property_get("audio.offload.vorbis.24b.enable", value, NULL)
                && (!strcmp("1", value) || !strcasecmp("true", value))) {
            ALOGV("dsp vorbis decoder operates in 24bit");
            *bitWidth = 24;
        }
    }
}

} // namespace android
#else //ENABLE_AV_ENHANCEMENTS

namespace android {

sp<MetaData> ExtendedUtils::updatePCMFormatAndBitwidth(
                sp<MediaSource> &audioSource, bool offloadAudio)
{
    ARG_TOUCH(audioSource);
    ARG_TOUCH(offloadAudio);
    sp<MetaData> tempMetadata = new MetaData;
    return tempMetadata;
}

void ExtendedUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params, sp<MetaData> &meta) {
    ARG_TOUCH(params);
    ARG_TOUCH(meta);
}

status_t ExtendedUtils::HFR::initializeHFR(
        const sp<MetaData> &meta, sp<AMessage> &format,
        int64_t &maxFileDurationUs, video_encoder videoEncoder) {
    ARG_TOUCH(meta);
    ARG_TOUCH(format);
    ARG_TOUCH(maxFileDurationUs);
    ARG_TOUCH(videoEncoder);
    return OK;
}

void ExtendedUtils::HFR::setHFRRatio(
        sp<MetaData> &meta, const int32_t hfrRatio) {
    ARG_TOUCH(meta);
    ARG_TOUCH(hfrRatio);
}

int32_t ExtendedUtils::HFR::getHFRRatio(
        const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return 1;
}

int32_t ExtendedUtils::HFR::getHFRCapabilities(
        video_encoder codec,
        int& maxHFRWidth, int& maxHFRHeight, int& maxHFRFps,
        int& maxBitRate) {
    ARG_TOUCH(codec);
    maxHFRWidth = maxHFRHeight = maxHFRFps = maxBitRate = 0;
    return -1;
}

bool ExtendedUtils::ShellProp::isAudioDisabled(bool isEncoder) {
    return false;
}

void ExtendedUtils::ShellProp::setEncoderProfile(
        video_encoder &videoEncoder, int32_t &videoEncoderProfile,
        int32_t &videoEncoderLevel) {
    ARG_TOUCH(videoEncoder);
    ARG_TOUCH(videoEncoderProfile);
    ARG_TOUCH(videoEncoderLevel);
}

int64_t ExtendedUtils::ShellProp::getMaxAVSyncLateMargin() {
     return kDefaultAVSyncLateMargin;
}

bool ExtendedUtils::ShellProp::isSmoothStreamingEnabled() {
    return false;
}

void ExtendedUtils::ShellProp::getRtpPortRange(unsigned *start, unsigned *end) {
    *start = kDefaultRtpPortRangeStart;
    *end = kDefaultRtpPortRangeEnd;
}

bool ExtendedUtils::ShellProp::getSTAProxyConfig(int32_t &port) {
    return false;
}

wp<ExtendedUtils::DiscoverProxy> ExtendedUtils::DiscoverProxy::gDProxy = NULL;
Mutex ExtendedUtils::DiscoverProxy::gLock;
bool ExtendedUtils::DiscoverProxy::gSetStopProxyInProgress = false;
Condition ExtendedUtils::DiscoverProxy::gCondition;
sp<ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient> ExtendedUtils::DiscoverProxy::gDeathNotifier = NULL;

ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient::~STAProxyServiceDeathRecepient() {
}

void
ExtendedUtils::DiscoverProxy::STAProxyServiceDeathRecepient::binderDied(const wp<IBinder>& who) {
}

void ExtendedUtils::DiscoverProxy::create(
        sp<ExtendedUtils::DiscoverProxy> *proxy,
        KeyedVector<String8, String8> *hdr,
        const KeyedVector<String8, String8> *headers) {
    return;
}

status_t ExtendedUtils::DiscoverProxy::checkForProxyAvail(
         sp<ExtendedUtils::DiscoverProxy> proxy,
         KeyedVector<String8, String8> *headers,
         int32_t *count, uint32_t what,
         const AString &url, ALooper::handler_id target) {
    return OK;
}

status_t ExtendedUtils::DiscoverProxy::checkProxyAvail(
         KeyedVector<String8, String8> *headers,
         int32_t *count, int64_t *delayUs) {
    return OK;
}

bool ExtendedUtils::DiscoverProxy::isProxySet(
         const KeyedVector<String8, String8>& headers) {
    return false;
}

sp<ExtendedUtils::DiscoverProxy> ExtendedUtils::DiscoverProxy::create() {
    return NULL;
}

ExtendedUtils::DiscoverProxy::DiscoverProxy(bool& bOk) {
    bOk = false;
}

ExtendedUtils::DiscoverProxy::~DiscoverProxy() {
}

bool ExtendedUtils::DiscoverProxy::getSTAProxyConfig(int32_t &port) {
    port = -1;
    return false;
}

bool ExtendedUtils::DiscoverProxy::sendSTAProxyStopIntent() {
    return false;
}

bool ExtendedUtils::DiscoverProxy::sendSTAProxyStartIntent() {
    return false;
}

bool ExtendedUtils::ShellProp::isCustomHLSEnabled() {
    return false;
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, const char* componentName) {
    ARG_TOUCH(mpeg4type);
    ARG_TOUCH(componentName);
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, const int32_t iFramesInterval,
        const int32_t frameRate, const char* componentName) {
    ARG_TOUCH(h264type);
    ARG_TOUCH(iFramesInterval);
    ARG_TOUCH(frameRate);
    ARG_TOUCH(componentName);
}

bool ExtendedUtils::UseQCHWAACEncoder(audio_encoder Encoder,int32_t Channel,
        int32_t BitRate,int32_t SampleRate) {
    ARG_TOUCH(Encoder);
    ARG_TOUCH(Channel);
    ARG_TOUCH(BitRate);
    ARG_TOUCH(SampleRate);
    return false;
}

bool ExtendedUtils::is24bitPCMOffloadEnabled() {
    return false;
}

bool ExtendedUtils::is16bitPCMOffloadEnabled() {
    return false;
}

bool ExtendedUtils::isTrackOffloadEnabled() {
    return false;
}

bool ExtendedUtils::isRAWFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

bool ExtendedUtils::isRAWFormat(const sp<AMessage> &format) {
    ARG_TOUCH(format);
    return false;
}

int32_t ExtendedUtils::getPcmSampleBits(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return 16;
}

int32_t ExtendedUtils::getPcmSampleBits(const sp<AMessage> &format) {
    ARG_TOUCH(format);
    return 16;
}

int32_t ExtendedUtils::getPCMFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

void ExtendedUtils::setKeyPCMFormat(const sp<MetaData> &meta, int32_t pcmFormat) {
    ARG_TOUCH(meta);
    ARG_TOUCH(pcmFormat);
}

status_t ExtendedUtils::convertToSinkFormat(const sp<ABuffer> &buffer, sp<ABuffer> &newBuffer,
                                                 audio_format_t srcFormat, audio_format_t pcmFormat,
                                                 bool isOffload) {
    ARG_TOUCH(buffer);
    ARG_TOUCH(newBuffer);
    ARG_TOUCH(srcFormat);
    ARG_TOUCH(pcmFormat);
    ARG_TOUCH(isOffload);
    return OK;
}

sp<MediaExtractor> ExtendedUtils::MediaExtractor_CreateIfNeeded(
        sp<MediaExtractor> defaultExt,
        const sp<DataSource> &source,
        const char *mime) {
    ARG_TOUCH(defaultExt);
    ARG_TOUCH(source);
    ARG_TOUCH(mime);
    return defaultExt;
}

bool ExtendedUtils::isAVCProfileSupported(int32_t  profile) {
    ARG_TOUCH(profile);
    return false;
}

void ExtendedUtils::updateNativeWindowBufferGeometry(ANativeWindow* anw,
        OMX_U32 width, OMX_U32 height, OMX_COLOR_FORMATTYPE colorFormat) {
    ARG_TOUCH(anw);
    ARG_TOUCH(width);
    ARG_TOUCH(height);
    ARG_TOUCH(colorFormat);
}

bool ExtendedUtils::checkIsThumbNailMode(const uint32_t flags, char* componentName) {
    ARG_TOUCH(flags);
    ARG_TOUCH(componentName);
    return false;
}

void ExtendedUtils::HEVCMuxer::writeHEVCFtypBox(MPEG4Writer *writer) {
    ARG_TOUCH(writer);
}

status_t ExtendedUtils::HEVCMuxer::makeHEVCCodecSpecificData(const uint8_t *data, size_t size,
                                 void** codecSpecificData, size_t *codecSpecificDataSize) {
    ARG_TOUCH(data);
    ARG_TOUCH(size);
    ARG_TOUCH(codecSpecificData);
    *codecSpecificDataSize = 0;
    return BAD_VALUE;
}

void ExtendedUtils::HEVCMuxer::beginHEVCBox(MPEG4Writer *writer) {
    ARG_TOUCH(writer);
}

void ExtendedUtils::HEVCMuxer::writeHvccBox(MPEG4Writer *writer, void* codecSpecificData,
                  size_t codecSpecificDataSize, bool useNalLengthFour) {
    ARG_TOUCH(writer);
    ARG_TOUCH(codecSpecificData);
    ARG_TOUCH(codecSpecificDataSize);
    ARG_TOUCH(useNalLengthFour);
}

bool ExtendedUtils::HEVCMuxer::isVideoHEVC(const char* mime) {
    ARG_TOUCH(mime);
    return false;
}

bool ExtendedUtils::HEVCMuxer::getHEVCCodecConfigData(const sp<MetaData> &meta,
                  const void **data, size_t *size) {
    ARG_TOUCH(meta);
    ARG_TOUCH(data);
    ARG_TOUCH(size);
    *size = 0;
    return false;
}

void ExtendedUtils::applyPreRotation(
        const CameraParameters&, sp<MetaData>&) {}

void ExtendedUtils::updateVideoTrackInfoFromESDS_MPEG4Video(sp<MetaData> meta) {
    ARG_TOUCH(meta);
}

bool ExtendedUtils::checkDPFromCodecSpecificData(const uint8_t *data, size_t size) {
    ARG_TOUCH(data);
    ARG_TOUCH(size);
    return false;
}

bool ExtendedUtils::checkDPFromVOLHeader(const uint8_t *data, size_t size) {
    ARG_TOUCH(data);
    ARG_TOUCH(size);
    return false;
}

int32_t ExtendedUtils::getEncoderTypeFlags() {
    return false;
}

void ExtendedUtils::cacheCaptureBuffers(sp<ICamera> camera, video_encoder encoder) {}

void ExtendedUtils::detectAndPostImage(const sp<ABuffer> accessUnit,
        const sp<AMessage> &notify) {
    ARG_TOUCH(accessUnit);
    ARG_TOUCH(notify);
}

void ExtendedUtils::showImageInNativeWindow(const sp<AMessage> &msg,
        const sp<AMessage> &format) {
    ARG_TOUCH(msg);
    ARG_TOUCH(format);
}

bool ExtendedUtils::RTSPStream::ParseURL_V6(
        AString *host, const char **colonPos) {
    return false;
}

void ExtendedUtils::RTSPStream::MakePortPair_V6(
        int *rtpSocket, int *rtcpSocket, unsigned *rtpPort){}

bool ExtendedUtils::RTSPStream::pokeAHole_V6(int rtpSocket, int rtcpSocket,
        const AString &transport, AString &sessionHost) {
    return false;
}

void ExtendedUtils::RTSPStream::notifyBye(const sp<AMessage> &msg, const int32_t what) {}

void ExtendedUtils::RTSPStream::bumpSocketBufferSize_V6(int s) {}

bool ExtendedUtils::RTSPStream::GetAttribute(const char *s, const char *key, AString *value) {
    return false;
}

void ExtendedUtils::RTSPStream::addRR(const sp<ABuffer> &buf) {}

void ExtendedUtils::RTSPStream::addSDES(int s, const sp<ABuffer> &buffer) {}

//return true to make sure pcm offload is not exercised
bool ExtendedUtils::pcmOffloadException(const char* const mime) {
    ARG_TOUCH(mime);
    return true;
}

sp<MetaData> ExtendedUtils::createPCMMetaFromSource(
                const sp<MetaData> &sMeta) {
    ARG_TOUCH(sMeta);
    sp<MetaData> tPCMMeta = new MetaData;
    return tPCMMeta;
}

void ExtendedUtils::overWriteAudioFormat(
                sp<AMessage> &dst, const sp<AMessage> &src)
{
    ARG_TOUCH(dst);
    ARG_TOUCH(src);
    return;
}
bool ExtendedUtils::is24bitPCMOffloaded(const sp<MetaData> &sMeta) {
    ARG_TOUCH(sMeta);

    return false;
}

bool ExtendedUtils::isVorbisFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

size_t ExtendedUtils::getVorbisHdrSize(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return 0;
}

sp<ABuffer> ExtendedUtils::assembleVorbisHdr(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return NULL;
}

bool ExtendedUtils::isWMAFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;

}

bool ExtendedUtils::isAudioWMAPro(const sp<AMessage> &format) {
    ARG_TOUCH(format);
    return false;
}

status_t ExtendedUtils::getWMAVersion(const sp<MetaData> &meta, int32_t *version) {
    ARG_TOUCH(meta);
    ARG_TOUCH(version);
    return BAD_VALUE;
}

status_t ExtendedUtils::sendMetaDataToHal(const sp<MetaData> &meta, AudioParameter *param) {
    ARG_TOUCH(meta);
    ARG_TOUCH(param);
    return OK;
}

bool ExtendedUtils::isALACFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

bool ExtendedUtils::isAPEFormat(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

bool ExtendedUtils::checkAPECompressionLevel(const sp<MetaData> &meta) {
    ARG_TOUCH(meta);
    return false;
}

bool ExtendedUtils::isHwAudioDecoderSessionAllowed(const char *mime) {
    ARG_TOUCH(mime);
    return true;
}
#ifdef DTS_CODEC_M_
size_t ExtendedUtils::DTS::parseDTSSyncFrame(const uint8_t *ptr, size_t size,
                                  sp<MetaData> *metaData) {
    ARG_TOUCH(ptr);
    ARG_TOUCH(size);
    ARG_TOUCH(metaData);
    return 0;
}

bool ExtendedUtils::DTS::IsSeeminglyValidDTSHeader(const uint8_t *ptr, size_t size) {
    ARG_TOUCH(ptr);
    ARG_TOUCH(size);
    return false;
}
#endif //DTS_CODEC_M_

sp<MediaCodec> ExtendedUtils::CreateCustomComponentByName(const sp<ALooper> &looper,
                        const char* mime, bool encoder) {
    ARG_TOUCH(looper);
    ARG_TOUCH(mime);
    ARG_TOUCH(encoder);

    return NULL;
}

void ExtendedUtils::extractBitWidth(const sp<AMessage> &format,
                        audio_format_t audioFormat, int32_t *bitWidth) {
    ARG_TOUCH(format);
    ARG_TOUCH(audioFormat);
    ARG_TOUCH(bitWidth);

    return;
}

} // namespace android
#endif //ENABLE_AV_ENHANCEMENTS

// Methods with identical implementation with & without ENABLE_AV_ENHANCEMENTS
namespace android {

bool ExtendedUtils::isVideoMuxFormatSupported(const char *mime) {
    if (mime == NULL) {
        ALOGE("NULL video mime type");
        return false;
    }

    if (!strcasecmp(MEDIA_MIMETYPE_VIDEO_MPEG4, mime)
            || !strcasecmp(MEDIA_MIMETYPE_VIDEO_H263, mime)
            || !strcasecmp(MEDIA_MIMETYPE_VIDEO_AVC, mime)
            || !strcasecmp(MEDIA_MIMETYPE_VIDEO_HEVC, mime)) {
        return true;
    }

    return false;
}

bool ExtendedUtils::isAudioMuxFormatSupported(const char *mime) {
    if (mime == NULL) {
        ALOGE("NULL audio mime type");
        return false;
    }

    if (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_NB, mime)
            || !strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_WB, mime)
            || !strcasecmp(MEDIA_MIMETYPE_AUDIO_AAC, mime)) {
        return true;
    }
    return false;
}

void ExtendedUtils::printFileName(int fd) {
    if (fd) {
        char prop[PROPERTY_VALUE_MAX];
        if (property_get("media.stagefright.log-uri", prop, "false") &&
                (!strcmp(prop, "1") || !strcmp(prop, "true"))) {

            char symName[40] = {0};
            char fileName[256] = {0};
            snprintf(symName, sizeof(symName), "/proc/%d/fd/%d", getpid(), fd);

            if (readlink( symName, fileName, (sizeof(fileName) - 1)) != -1 ) {
                ALOGI("printFileName fd(%d) -> %s", fd, fileName);
            }
        }
    }
}

bool ExtendedUtils::isAudioAMR(const char* mime) {
    if (mime && (!strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_NB, mime)
            || !strcasecmp(MEDIA_MIMETYPE_AUDIO_AMR_WB, mime))) {
        return true;
    }

    return false;
}
}
