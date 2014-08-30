/*Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
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

#include <media/stagefright/MetaData.h>
#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/OMXCodec.h>
#include <cutils/properties.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/MediaProfiles.h>

#include "include/ExtendedUtils.h"

static const int64_t kDefaultAVSyncLateMargin =  40000;
static const int64_t kMaxAVSyncLateMargin     = 250000;

static const unsigned kDefaultRtpPortRangeStart = 15550;
static const unsigned kDefaultRtpPortRangeEnd = 65535;
static const uint8_t kHEVCNalUnitTypeVidParamSet = 0x20;
static const uint8_t kHEVCNalUnitTypeSeqParamSet = 0x21;
static const uint8_t kHEVCNalUnitTypePicParamSet = 0x22;

#ifdef ENABLE_AV_ENHANCEMENTS

#include <QCMetaData.h>
#include <QCMediaDefs.h>

#include "include/ExtendedExtractor.h"
#include "include/avc_utils.h"
#include <fcntl.h>
#include <linux/msm_ion.h>
#define MEM_DEVICE "/dev/ion"
#define MEM_HEAP_ID ION_CP_MM_HEAP_ID
#define FLAG_COPY_ENABLE 'CpEn'

#include <media/stagefright/foundation/ALooper.h>

namespace android {

void ExtendedUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params,
        sp<MetaData> &meta) {
    const char *hfr_str = params.get("video-hfr");
    int32_t hfr = -1;
    if ( hfr_str != NULL ) {
        hfr = atoi(hfr_str);
        if(hfr > 0) {
            ALOGI("HFR enabled, %d value provided", hfr);
            meta->setInt32(kKeyHFR, hfr);
            return;
        } else {
            ALOGI("Invalid hfr value(%d) set from app. Disabling HFR.", hfr);
        }
    }

    const char *hsr_str = params.get("video-hsr");
    int32_t hsr = -1;
    if(hsr_str != NULL ) {
        hsr = atoi(hsr_str);
        if(hsr > 0) {
            ALOGI("HSR enabled, %d value provided", hsr);
            meta->setInt32(kKeyHSR, hsr);
            return;
        } else {
            ALOGI("Invalid hsr value(%d) set from app. Disabling HSR.", hsr);
        }
    }
}

status_t ExtendedUtils::HFR::initializeHFR(
        sp<MetaData> &meta, sp<MetaData> &enc_meta,
        int64_t &maxFileDurationUs, video_encoder videoEncoder) {
    status_t retVal = OK;

    //Check HSR first, if HSR is enable set HSR to kKeyFrameRate
    int32_t hsr =0;
    if (meta->findInt32(kKeyHSR, &hsr)) {
        ALOGI("HSR found %d, set this to encoder frame rate",hsr);
        enc_meta->setInt32(kKeyFrameRate, hsr);
        return retVal;
    }

    int32_t hfr = 0;
    if (!meta->findInt32(kKeyHFR, &hfr)) {
        ALOGW("hfr not found, default to 0");
    }

    enc_meta->setInt32(kKeyHFR, hfr);

    if (hfr == 0) {
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
                "[%d x %d @%d fps]",
                width, height, hfr, maxW, maxH, MaxFrameRate);
        return ERROR_UNSUPPORTED;
    }

    int32_t frameRate = 0, bitRate = 0;
    CHECK(meta->findInt32(kKeyFrameRate, &frameRate));
    CHECK(enc_meta->findInt32(kKeyBitRate, &bitRate));

    if (frameRate) {
        // scale the bitrate proportional to the hfr ratio
        // to maintain quality, but cap it to max-supported.
        bitRate = (hfr * bitRate) / frameRate;
        bitRate = bitRate > maxBitRate ? maxBitRate : bitRate;
        enc_meta->setInt32(kKeyBitRate, bitRate);

        int32_t hfrRatio = hfr / frameRate;
        enc_meta->setInt32(kKeyFrameRate, hfr);
        enc_meta->setInt32(kKeyHFR, hfrRatio);
    } else {
        ALOGE("HFR: Invalid framerate");
        return BAD_VALUE;
    }

    return retVal;
}

void ExtendedUtils::HFR::copyHFRParams(
        const sp<MetaData> &inputFormat,
        sp<MetaData> &outputFormat) {
    int32_t frameRate = 0, hfr = 0;
    inputFormat->findInt32(kKeyHFR, &hfr);
    inputFormat->findInt32(kKeyFrameRate, &frameRate);
    outputFormat->setInt32(kKeyHFR, hfr);
    outputFormat->setInt32(kKeyFrameRate, frameRate);
}

int32_t ExtendedUtils::HFR::getHFRRatio(
        const sp<MetaData> &meta) {
    int32_t hfr = 0;
    meta->findInt32(kKeyHFR, &hfr);
    return hfr ? hfr : 1;
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
    if (codecSpecificData == NULL) {
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
        video_encoder &videoEncoder, int32_t &videoEncoderProfile) {
    char value[PROPERTY_VALUE_MAX];
    bool customProfile = false;
    if (!property_get("encoder.video.profile", value, NULL) > 0) {
        return;
    }

    switch (videoEncoder) {
        case VIDEO_ENCODER_H264:
            if (strncmp("base", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileBaseline;
                ALOGI("H264 Baseline Profile");
            } else if (strncmp("main", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileMain;
                ALOGI("H264 Main Profile");
            } else if (strncmp("high", value, 4) == 0) {
                videoEncoderProfile = OMX_VIDEO_AVCProfileHigh;
                ALOGI("H264 High Profile");
            } else {
                ALOGW("Unsupported H264 Profile");
            }
            break;
        case VIDEO_ENCODER_MPEG_4_SP:
            if (strncmp("simple", value, 5) == 0 ) {
                videoEncoderProfile = OMX_VIDEO_MPEG4ProfileSimple;
                ALOGI("MPEG4 Simple profile");
            } else if (strncmp("asp", value, 3) == 0 ) {
                videoEncoderProfile = OMX_VIDEO_MPEG4ProfileAdvancedSimple;
                ALOGI("MPEG4 Advanced Simple Profile");
            } else {
                ALOGW("Unsupported MPEG4 Profile");
            }
            break;
        default:
            ALOGW("No custom profile support for other codecs");
            break;
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

bool ExtendedUtils::ShellProp::isCustomAVSyncEnabled() {
    char prop[PROPERTY_VALUE_MAX] = {0};
    property_get("mm.enable.customavsync", prop, "0");
    if (!strncmp(prop, "true", 4) || atoi(prop)) {
        return true;
    }
    return false;
}

bool ExtendedUtils::ShellProp::isMpeg4DPSupportedByHardware() {
    char prop[PROPERTY_VALUE_MAX] = {0};
    property_get("mm.mpeg4dp.hw.support", prop, "0");
    if (!strncmp(prop, "true", 4) || atoi(prop)) {
        return true;
    }
    return false;
}

void ExtendedUtils::ShellProp::checkMemCpyOptimization(bool *avoidMemCpy,
                                           int32_t *addAdditionalBuffers) {
    if (avoidMemCpy == NULL) {
        ALOGE("Invalid i\p passed");
        return;
    }

    *avoidMemCpy = false;

    //look for the writer property
    char prop[PROPERTY_VALUE_MAX] = {0};
    property_get("mm.avoidmemcpy.enable", prop, "0");
    if (!strncmp(prop, "true", 4) || atoi(prop)) {
        *avoidMemCpy = true;
    }

    // check and set addAdditionalBuffer even if avoidMemCpy flag is false
    if (addAdditionalBuffers == NULL) {
        return;
    }

    property_get("mm.avoidmemcpy.addbuffers", prop, "0");
    *addAdditionalBuffers = atoi(prop);

    if (*addAdditionalBuffers <= 0) {
        *addAdditionalBuffers = 0;
        *avoidMemCpy = false;
    }
    ALOGV("ShellProp::checkMemCpyOptimization :: AvoidMemCpyEnable (%s)
           Additional buffers added (%d)", *avoidMemCpy? "TRUE" : "FALSE",
           *addAdditionalBuffers);
}

void ExtendedUtils::ShellProp::adjustInterleaveDuration(uint32_t *interleaveDuration) {
    bool avoidMemCpyEnable = false;
    ShellProp::checkMemCpyOptimization(&avoidMemCpyEnable);

    // modify interleave only if avoidMemCpyEnable is enabled, because it's already initialized
    if (avoidMemCpyEnable) {
        char prop[PROPERTY_VALUE_MAX] = {0};
        property_get("mm.avoidmemcpy.interleavedur", prop, "0");
        uint32_t newInterleaveVal = atoi(prop);

        if(newInterleaveVal > 0) {
            *interleaveDuration = newInterleaveVal;
        }
    }

    ALOGV("adjustInterleaveDuration :: interleavingDuration (%d)", *interleaveDuration);
}

bool ExtendedUtils::checkCopyFlagInBuffer(MediaBuffer *buffer) {
    int32_t copyIndication = 0;

    if (buffer != NULL) {
        buffer->meta_data()->findInt32(FLAG_COPY_ENABLE, &copyIndication);
    }

    return (bool)copyIndication;
}

void ExtendedUtils::checkAndSetIfBufferNeedsToBeCopied(Vector<OMXCodec::BufferInfo> *buffers,
                                                       OMXCodec::BufferInfo **info,
                                                       int32_t additionalBuff) {
    ALOGV("checkAndSetIfBufferNeedsToBeCopied called");

    if (!buffers || !(*info)) {
        ALOGE("Invalid i\p passed");
        return;
    }

    int withUs = 0, withComponent = 0, withClient = 0;
    for (size_t i = 0; i < buffers->size(); ++i) {
        OMXCodec::BufferInfo *tmpBuf = &buffers->editItemAt(i);
        if (tmpBuf->mStatus == OMXCodec::OWNED_BY_US) {
            ++withUs;
        } else if(tmpBuf->mStatus == OMXCodec::OWNED_BY_CLIENT) {
            ++withClient;
        } else if(tmpBuf->mStatus == OMXCodec::OWNED_BY_COMPONENT) {
            ++withComponent;
        }
    }

    ALOGV("read:: Buffers Owned by Us (%d) Client (%d) Component (%d)",
           withUs, withClient, withComponent);

    if (withClient < additionalBuff) {
        //indicate not to copy unless we run out of extra buffers
        (*info)->mMediaBuffer->meta_data()->setInt32(FLAG_COPY_ENABLE, 0);
    } else {
        (*info)->mMediaBuffer->meta_data()->setInt32(FLAG_COPY_ENABLE, 1);
    }
    return;
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, int32_t &numBFrames,
        const char* componentName) {
    //ignore non QC components
    if (strncmp(componentName, "OMX.qcom.", 9)) {
        return;
    }
    if (mpeg4type.eProfile > OMX_VIDEO_MPEG4ProfileSimple) {
        mpeg4type.nAllowedPictureTypes |= OMX_VIDEO_PictureTypeB;
        mpeg4type.nBFrames = 1;
        mpeg4type.nPFrames /= (mpeg4type.nBFrames + 1);
        numBFrames = mpeg4type.nBFrames;
    }
    return;
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, int32_t &numBFrames,
        int32_t iFramesInterval, int32_t frameRate, const char* componentName) {
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
        h264type.nBFrames = 1;
        h264type.nPFrames /= (h264type.nBFrames + 1);
        //enable CABAC as default entropy mode for Hihg/Main profiles
        h264type.bEntropyCodingCABAC = OMX_TRUE;
        h264type.nCabacInitIdc = 0;
        numBFrames = h264type.nBFrames;
    }
    return;
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
    bool ret = false;
    int minBiteRate = -1;
    int maxBiteRate = -1;
    char propValue[PROPERTY_VALUE_MAX] = {0};

    property_get("qcom.hw.aac.encoder",propValue,NULL);
    if (!strncmp(propValue,"true",sizeof("true"))) {
        //check for QCOM's HW AAC encoder only when qcom.aac.encoder =  true;
        ALOGV("qcom.aac.encoder enabled, check AAC encoder(%d) allowed bitrates",Encoder);
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
            if (Channel == 1) {//mono
                minBiteRate = MIN_BITERATE_AAC;
                maxBiteRate = MAX_BITERATE_AAC<(SampleRate*6)?MAX_BITERATE_AAC:(SampleRate*6);
            } else if (Channel == 2) {//stereo
                minBiteRate = MIN_BITERATE_AAC;
                maxBiteRate = MAX_BITERATE_AAC<(SampleRate*12)?MAX_BITERATE_AAC:(SampleRate*12);
            }
            break;
        default:
            ALOGV("encoder:%d not supported by QCOM HW AAC encoder",Encoder);

        }

        //return true only when 1. minBiteRate and maxBiteRate are updated(not -1) 2. minBiteRate <= SampleRate <= maxBiteRate
        if (BitRate >= minBiteRate && BitRate <= maxBiteRate) {
            ret = true;
        }
    }

    return ret;
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
    int  numOfTrack              = 0;

    if (defaultExt != NULL) {
        for (size_t trackItt = 0; trackItt < defaultExt->countTracks(); ++trackItt) {
            ++numOfTrack;
            sp<MetaData> meta = defaultExt->getTrackMetaData(trackItt);
            const char *_mime;
            CHECK(meta->findCString(kKeyMIMEType, &_mime));

            String8 mime = String8(_mime);

            if (!strncasecmp(mime.string(), "audio/", 6)) {
                audioTrackFound = true;

                amrwbAudio = !strncasecmp(mime.string(),
                                          MEDIA_MIMETYPE_AUDIO_AMR_WB,
                                          strlen(MEDIA_MIMETYPE_AUDIO_AMR_WB));
                if (amrwbAudio) {
                    break;
                }
            }else if(!strncasecmp(mime.string(), "video/", 6)) {
                videoTrackFound = true;
            }
        }

        if(amrwbAudio) {
            bCheckExtendedExtractor = true;
        }else if (numOfTrack  == 0) {
            bCheckExtendedExtractor = true;
        } else if(numOfTrack == 1) {
            if((videoTrackFound) ||
                (!videoTrackFound && !audioTrackFound)){
                bCheckExtendedExtractor = true;
            }
        } else if (numOfTrack >= 2){
            if(videoTrackFound && audioTrackFound) {
                if(amrwbAudio) {
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

    for (size_t trackItt = 0; (trackItt < retExtExtractor->countTracks()); ++trackItt) {
        sp<MetaData> meta = retExtExtractor->getTrackMetaData(trackItt);
        const char *mime;
        bool success = meta->findCString(kKeyMIMEType, &mime);
        if ((success == true) &&
            (!strncasecmp(mime, MEDIA_MIMETYPE_AUDIO_AMR_WB_PLUS,
                                strlen(MEDIA_MIMETYPE_AUDIO_AMR_WB_PLUS)) ||
             !strncasecmp(mime, MEDIA_MIMETYPE_VIDEO_HEVC,
                                strlen(MEDIA_MIMETYPE_VIDEO_HEVC)) )) {

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

void ExtendedUtils::helper_addMediaCodec(Vector<MediaCodecList::CodecInfo> &mCodecInfos,
                                          KeyedVector<AString, size_t> &mTypes,
                                          bool encoder, const char *name,
                                          const char *type, uint32_t quirks) {
    mCodecInfos.push();
    MediaCodecList::CodecInfo *info = &mCodecInfos.editItemAt(mCodecInfos.size() - 1);
    info->mName = name;
    info->mIsEncoder = encoder;
    info->mTypes = 0;
    ssize_t index = mTypes.indexOfKey(type);
    uint32_t bit;

    if(index < 0) {
        bit = mTypes.size();
        if (bit == 32) {
            ALOGW("Too many distinct type names in configuration.");
            return;
        }
        mTypes.add(name, bit);
    } else {
        bit = mTypes.valueAt(index);
    }
    info->mTypes = 1ul << bit;
    info->mQuirks = quirks;
}

uint32_t ExtendedUtils::helper_getCodecSpecificQuirks(KeyedVector<AString, size_t> &mCodecQuirks,
                                                       Vector<AString> quirks) {
    size_t i = 0, numQuirks = quirks.size();
    uint32_t bit = 0, value = 0;
    for (i = 0; i < numQuirks; i++)
    {
        ssize_t index = mCodecQuirks.indexOfKey(quirks.itemAt(i));
        bit = mCodecQuirks.valueAt(index);
        value |= 1ul << bit;
    }
    return value;
}

bool ExtendedUtils::isAVCProfileSupported(int32_t  profile){
   if(profile == OMX_VIDEO_AVCProfileMain || profile == OMX_VIDEO_AVCProfileHigh || profile == OMX_VIDEO_AVCProfileBaseline){
      return true;
   } else {
      return false;
   }
}

void ExtendedUtils::updateNativeWindowBufferGeometry(ANativeWindow* anw,
        OMX_U32 width, OMX_U32 height, OMX_COLOR_FORMATTYPE colorFormat) {
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

void ExtendedUtils::helper_Mpeg4ExtractorCheckAC3EAC3(MediaBuffer *buffer,
                                                        sp<MetaData> &format,
                                                        size_t size) {
    bool mMakeBigEndian = false;
    const char *mime;

    if (format->findCString(kKeyMIMEType, &mime)
            && (!strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_AC3) ||
            !strcasecmp(mime, MEDIA_MIMETYPE_AUDIO_EAC3))) {
        mMakeBigEndian = true;
    }
    if (mMakeBigEndian && *((uint8_t *)buffer->data())==0x0b &&
            *((uint8_t *)buffer->data()+1)==0x77 ) {
        size_t count = 0;
        for(count=0;count<size;count+=2) { // size is always even bytes in ac3/ec3 read
            uint8_t tmp = *((uint8_t *)buffer->data() + count);
            *((uint8_t *)buffer->data() + count) = *((uint8_t *)buffer->data()+count+1);
            *((uint8_t *)buffer->data() + count+1) = tmp;
        }
    }
}

int32_t ExtendedUtils::getEncoderTypeFlags() {
    return OMXCodec::kHardwareCodecsOnly;
}

void ExtendedUtils::prefetchSecurePool(const char *uri)
{
    if (!uri) {
        return;
    }

    if (!strncasecmp("widevine://", uri, 11)) {
        ALOGV("Widevine streaming content\n");
        createSecurePool();
    }
}

void ExtendedUtils::prefetchSecurePool(int fd)
{
    char symName[40] = {0};
    char fileName[256] = {0};
    char* kSuffix;
    size_t kSuffixLength = 0;
    size_t fileNameLength;

    snprintf(symName, sizeof(symName), "/proc/%d/fd/%d", getpid(), fd);

    if (readlink( symName, fileName, (sizeof(fileName) - 1)) != -1 ) {
        kSuffix = (char *)".wvm";
        kSuffixLength = strlen(kSuffix);
        fileNameLength = strlen(fileName);

        if (!strcmp(&fileName[fileNameLength - kSuffixLength], kSuffix)) {
            ALOGV("Widevine local content\n");
            createSecurePool();
        }
    }
}

void ExtendedUtils::prefetchSecurePool()
{
    createSecurePool();
}

void ExtendedUtils::createSecurePool()
{
    struct ion_prefetch_data prefetch_data;
    struct ion_custom_data d;
    int ion_dev_flag = O_RDONLY;
    int rc = 0;
    int fd = open (MEM_DEVICE, ion_dev_flag);

    if (fd < 0) {
        ALOGE("opening ion device failed with fd = %d", fd);
    } else {
        prefetch_data.heap_id = ION_HEAP(MEM_HEAP_ID);
        prefetch_data.len = 0x0;
        d.cmd = ION_IOC_PREFETCH;
        d.arg = (unsigned long int)&prefetch_data;
        rc = ioctl(fd, ION_IOC_CUSTOM, &d);
        if (rc != 0) {
            ALOGE("creating secure pool failed, rc is %d, errno is %d", rc, errno);
        }
        close(fd);
    }
}

void ExtendedUtils::drainSecurePool()
{
    struct ion_prefetch_data prefetch_data;
    struct ion_custom_data d;
    int ion_dev_flag = O_RDONLY;
    int rc = 0;
    int fd = open (MEM_DEVICE, ion_dev_flag);

    if (fd < 0) {
        ALOGE("opening ion device failed with fd = %d", fd);
    } else {
        prefetch_data.heap_id = ION_HEAP(MEM_HEAP_ID);
        prefetch_data.len = 0x0;
        d.cmd = ION_IOC_DRAIN;
        d.arg = (unsigned long int)&prefetch_data;
        rc = ioctl(fd, ION_IOC_CUSTOM, &d);
        if (rc != 0) {
            ALOGE("draining secure pool failed rc is %d, errno is %d", rc, errno);
        }
        close(fd);
    }
}

VSyncLocker::VSyncLocker()
    : mExitVsyncEvent(true),
      mLooper(NULL),
      mSyncState(PROFILE_FPS),
      mStartTime(-1),
      mProfileCount(0) {
}

VSyncLocker::~VSyncLocker() {
    if(!mExitVsyncEvent) {
        mExitVsyncEvent = true;
        void *dummy;
        pthread_join(mThread, &dummy);
    }
}

bool VSyncLocker::isSyncRenderEnabled() {
    char value[PROPERTY_VALUE_MAX];
    bool ret = true;
    property_get("mm.enable.vsync.render", value, "0");
    if (atoi(value) == 0) {
        ret = false;
    }
    return ret;
}

void VSyncLocker::updateSyncState() {
    if (mSyncState == PROFILE_FPS) {
        mProfileCount++;
        if (mProfileCount == 1) {
            mStartTime = ALooper::GetNowUs();
        } else if (mProfileCount == kMaxProfileCount) {
            int fps = (kMaxProfileCount * 1000000) /
                      (ALooper::GetNowUs() - mStartTime);
            if (fps > 35) {
                ALOGI("Synchronized rendering blocked at %d fps", fps);
                mSyncState = BLOCK_SYNC;
                mExitVsyncEvent = true;
            } else {
                ALOGI("Synchronized rendering enabled at %d fps", fps);
                mSyncState = ENABLE_SYNC;
            }
        }
    }
}

void VSyncLocker::waitOnVSync() {
    Mutex::Autolock autoLock(mVsyncLock);
    mVSyncCondition.wait(mVsyncLock);
}

void VSyncLocker::resetProfile() {
    if (mSyncState == PROFILE_FPS) {
        mProfileCount = 0;
    }
}

void VSyncLocker::blockSync() {
    if (mSyncState == ENABLE_SYNC) {
        ALOGI("Synchronized rendering blocked");
        mSyncState = BLOCK_SYNC;
        mExitVsyncEvent = true;
    }
}

void VSyncLocker::blockOnVSync() {
        if (mSyncState == PROFILE_FPS) {
            updateSyncState();
        } else if(mSyncState == ENABLE_SYNC) {
            waitOnVSync();
        }
}

void VSyncLocker::start() {
    mExitVsyncEvent = false;
    mLooper = new Looper(false);
    mLooper->addFd(mDisplayEventReceiver.getFd(), 0,
                   ALOOPER_EVENT_INPUT, receiver, (void *)this);
    mDisplayEventReceiver.setVsyncRate(1);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_create(&mThread, &attr, ThreadWrapper, (void *)this);
    pthread_attr_destroy(&attr);
}

void VSyncLocker::VSyncEvent() {
    do {
        int ret = 0;
        if (mLooper != NULL) {
            ret = mLooper->pollOnce(-1);
        }
    } while (!mExitVsyncEvent);
    mDisplayEventReceiver.setVsyncRate(0);
    if (mLooper != NULL) {
        mLooper->removeFd(mDisplayEventReceiver.getFd());
    }
}

void VSyncLocker::signalVSync() {
   DisplayEventReceiver::Event buffer[1];
   if(mDisplayEventReceiver.getEvents(buffer, 1)) {
       if (buffer[0].header.type != DisplayEventReceiver::DISPLAY_EVENT_VSYNC) {
           return;
        }
   }
   mVsyncLock.lock();
   mVSyncCondition.signal();
   mVsyncLock.unlock();
   ALOGV("Signalling VSync");
}

void *VSyncLocker::ThreadWrapper(void *context) {
    VSyncLocker *renderer = (VSyncLocker *)context;
    renderer->VSyncEvent();
    return NULL;
}

int VSyncLocker::receiver(int fd, int events, void *context) {
    VSyncLocker *locker = (VSyncLocker *)context;
    locker->signalVSync();
    return 1;
}

void ExtendedUtils::parseRtpPortRangeFromSystemProperty(unsigned *start, unsigned *end) {
    char value[PROPERTY_VALUE_MAX];
    if (!property_get("persist.sys.media.rtp-ports", value, NULL)) {
        ALOGV("Cannot get property of persist.sys.media.rtp-ports");
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    if (sscanf(value, "%u/%u", start, end) != 2) {
        ALOGE("Failed to parse rtp port range from '%s'.", value);
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    if (*start > *end || *start <= 1024 || *end >= 65535) {
        ALOGE("Illegal rtp port start/end specified, reverting to defaults.");
        *start = kDefaultRtpPortRangeStart;
        *end = kDefaultRtpPortRangeEnd;
        return;
    }

    ALOGV("rtp port_start = %u, port_end = %u", *start, *end);
}

}
#else //ENABLE_AV_ENHANCEMENTS

namespace android {

void ExtendedUtils::HFR::setHFRIfEnabled(
        const CameraParameters& params, sp<MetaData> &meta) {
}

status_t ExtendedUtils::HFR::initializeHFR(
        sp<MetaData> &meta, sp<MetaData> &enc_meta,
        int64_t &maxFileDurationUs, video_encoder videoEncoder) {
    return OK;
}

void ExtendedUtils::HFR::copyHFRParams(
        const sp<MetaData> &inputFormat,
        sp<MetaData> &outputFormat) {
}

int32_t ExtendedUtils::HFR::getHFRRatio(
        const sp<MetaData> &meta) {
        return 0;
}

int32_t ExtendedUtils::HFR::getHFRCapabilities(
        video_encoder codec,
        int& maxHFRWidth, int& maxHFRHeight, int& maxHFRFps,
        int& maxBitRate) {
    maxHFRWidth = maxHFRHeight = maxHFRFps = maxBitRate = 0;
    return -1;
}

bool ExtendedUtils::ShellProp::isAudioDisabled(bool isEncoder) {
    return false;
}

void ExtendedUtils::ShellProp::setEncoderProfile(
        video_encoder &videoEncoder, int32_t &videoEncoderProfile) {
}

int64_t ExtendedUtils::ShellProp::getMaxAVSyncLateMargin() {
     return kDefaultAVSyncLateMargin;
}

bool ExtendedUtils::ShellProp::isSmoothStreamingEnabled() {
    return false;
}

bool ExtendedUtils::ShellProp::isCustomAVSyncEnabled() {
    return false;
}

bool ExtendedUtils::ShellProp::isMpeg4DPSupportedByHardware() {
    return false;
}

void ExtendedUtils::ShellProp::adjustInterleaveDuration(uint32_t *interleaveDuration) {
}

bool ExtendedUtils::checkCopyFlagInBuffer(MediaBuffer *buffer) {
    return false;
}

void ExtendedUtils::checkAndSetIfBufferNeedsToBeCopied(Vector<OMXCodec::BufferInfo> *buffers,
                                                       OMXCodec::BufferInfo **info,
                                                       int32_t additionalBuff) {
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_MPEG4TYPE &mpeg4type, int32_t &numBFrames,
        const char* componentName) {
}

void ExtendedUtils::setBFrames(
        OMX_VIDEO_PARAM_AVCTYPE &h264type, int32_t &numBFrames,
        int32_t iFramesInterval, int32_t frameRate,
        const char* componentName) {
}

bool ExtendedUtils::UseQCHWAACEncoder(audio_encoder Encoder,int32_t Channel,
    int32_t BitRate,int32_t SampleRate) {
    return false;
}

sp<MediaExtractor> ExtendedUtils::MediaExtractor_CreateIfNeeded(sp<MediaExtractor> defaultExt,
                                                            const sp<DataSource> &source,
                                                            const char *mime) {
    return defaultExt;
}

void ExtendedUtils::helper_addMediaCodec(Vector<MediaCodecList::CodecInfo> &mCodecInfos,
                                          KeyedVector<AString, size_t> &mTypes,
                                          bool encoder, const char *name,
                                          const char *type, uint32_t quirks) {
}

uint32_t ExtendedUtils::helper_getCodecSpecificQuirks(KeyedVector<AString, size_t> &mCodecQuirks,
                                                       Vector<AString> quirks) {
    return 0;
}

bool ExtendedUtils::isAVCProfileSupported(int32_t  profile){
     return false;
}

void ExtendedUtils::updateNativeWindowBufferGeometry(ANativeWindow* anw,
        OMX_U32 width, OMX_U32 height, OMX_COLOR_FORMATTYPE colorFormat) {
}

bool ExtendedUtils::checkIsThumbNailMode(const uint32_t flags, char* componentName) {
    return false;
}

void ExtendedUtils::helper_Mpeg4ExtractorCheckAC3EAC3(MediaBuffer *buffer,
                                                        sp<MetaData> &format,
                                                        size_t size) {
}

int32_t ExtendedUtils::getEncoderTypeFlags() {
    return 0;
}

void ExtendedUtils::prefetchSecurePool(int fd) {}

void ExtendedUtils::prefetchSecurePool(const char *uri) {}

void ExtendedUtils::prefetchSecurePool() {}

void ExtendedUtils::createSecurePool() {}

void ExtendedUtils::drainSecurePool() {}

void ExtendedUtils::ShellProp::checkMemCpyOptimization(bool *avoidMemCpy,
                                            int32_t *addAdditionalBuffer) {
    if (avoidMemCpy != NULL) {
        *avoidMemCpy = false;
    }
    if (addAdditionalBuffer != NULL) {
        *addAdditionalBuffer = 0;
    }
}

VSyncLocker::VSyncLocker() {}

VSyncLocker::~VSyncLocker() {}

bool VSyncLocker::isSyncRenderEnabled() {
    return false;
}

void *VSyncLocker::ThreadWrapper(void *context) {
    return NULL;
}

int VSyncLocker::receiver(int fd, int events, void *context) {
    return 0;
}

void VSyncLocker::updateSyncState() {}

void VSyncLocker::waitOnVSync() {}

void VSyncLocker::resetProfile() {}

void VSyncLocker::blockSync() {}

void VSyncLocker::blockOnVSync() {}

void VSyncLocker::start() {}

void VSyncLocker::VSyncEvent() {}

void VSyncLocker::signalVSync() {}

void ExtendedUtils::parseRtpPortRangeFromSystemProperty(unsigned *start, unsigned *end) {
    *start = kDefaultRtpPortRangeStart;
    *end = kDefaultRtpPortRangeEnd;
}

void ExtendedUtils::HEVCMuxer::writeHEVCFtypBox(MPEG4Writer *writer) {}

status_t ExtendedUtils::HEVCMuxer::makeHEVCCodecSpecificData(const uint8_t *data, size_t size,
                                 void** codecSpecificData, size_t *codecSpecificDataSize) {
    *codecSpecificDataSize = 0;
    return BAD_VALUE;
}

void ExtendedUtils::HEVCMuxer::beginHEVCBox(MPEG4Writer *writer) {}

void ExtendedUtils::HEVCMuxer::writeHvccBox(MPEG4Writer *writer, void* codecSpecificData,
                  size_t codecSpecificDataSize, bool useNalLengthFour) {}

bool ExtendedUtils::HEVCMuxer::isVideoHEVC(const char* mime) {
    return false;
}

bool ExtendedUtils::HEVCMuxer::getHEVCCodecConfigData(const sp<MetaData> &meta,
                  const void **data, size_t *size) {
    *size = 0;
    return false;
}

}
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

void ExtendedUtils::printFileName(int fd) {
    if (fd) {
        char symName[40] = {0};
        char fileName[256] = {0};
        snprintf(symName, sizeof(symName), "/proc/%d/fd/%d", getpid(), fd);

        if (readlink(symName, fileName, (sizeof(fileName) - 1)) != -1 ) {
            ALOGD("printFileName fd(%d) -> %s", fd, fileName);
        }
    }
}

void ExtendedUtils::printFileName(const char *uri) {
    if (uri) {
        ALOGD("printFileName %s", uri);
    }
}

}
