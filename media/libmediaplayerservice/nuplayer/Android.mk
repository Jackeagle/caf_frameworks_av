LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:=                       \
        GenericSource.cpp               \
        HTTPLiveSource.cpp              \
        NuPlayer.cpp                    \
        NuPlayerDecoder.cpp             \
        NuPlayerDecoderPassThrough.cpp  \
        NuPlayerDriver.cpp              \
        NuPlayerRenderer.cpp            \
        NuPlayerStreamListener.cpp      \
        RTSPSource.cpp                  \
        StreamingSource.cpp             \

LOCAL_C_INCLUDES := \
	$(TOP)/frameworks/av/media/libstagefright/httplive            \
	$(TOP)/frameworks/av/media/libstagefright/include             \
	$(TOP)/frameworks/av/media/libstagefright/mpeg2ts             \
	$(TOP)/frameworks/av/media/libstagefright/rtsp                \
	$(TOP)/frameworks/av/media/libstagefright/timedtext           \
	$(TOP)/frameworks/av/media/libmediaplayerservice              \
	$(TOP)/frameworks/native/include/media/openmax

LOCAL_MODULE:= libstagefright_nuplayer

ifeq ($(TARGET_ENABLE_QC_AV_ENHANCEMENTS),true)
    LOCAL_CFLAGS += -DENABLE_AV_ENHANCEMENTS
    LOCAL_C_INCLUDES += $(TOP)/hardware/qcom/media/mm-core/inc
ifeq ($(DTS_CODEC_M_), true)
       LOCAL_CFLAGS += -DDTS_CODEC_M_
endif
endif

LOCAL_MODULE_TAGS := eng

include $(BUILD_STATIC_LIBRARY)

