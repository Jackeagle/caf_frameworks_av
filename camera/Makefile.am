# Build libcamera_client.so
lib_LTLIBRARIES = libcamera_client.la

# Only enable Metadata for now.
libcamera_client_la_SOURCES := CameraMetadata.cpp
libcamera_client_la_SOURCES += CameraParameters.cpp
#libcamera_client_la_SOURCES += Camera.cpp
#libcamera_client_la_SOURCES += CaptureResult.cpp
#libcamera_client_la_SOURCES += CameraParameters2.cpp
#libcamera_client_la_SOURCES += ICamera.cpp
#libcamera_client_la_SOURCES += ICameraClient.cpp
#libcamera_client_la_SOURCES += ICameraService.cpp
#libcamera_client_la_SOURCES += ICameraServiceListener.cpp
#libcamera_client_la_SOURCES += ICameraServiceProxy.cpp
#libcamera_client_la_SOURCES += ICameraRecordingProxy.cpp
#libcamera_client_la_SOURCES += ICameraRecordingProxyListener.cpp
#libcamera_client_la_SOURCES += camera2/ICameraDeviceUser.cpp
#libcamera_client_la_SOURCES += camera2/ICameraDeviceCallbacks.cpp
#libcamera_client_la_SOURCES += camera2/CaptureRequest.cpp
#libcamera_client_la_SOURCES += camera2/OutputConfiguration.cpp
#libcamera_client_la_SOURCES += CameraBase.cpp
#libcamera_client_la_SOURCES += CameraUtils.cpp
#libcamera_client_la_SOURCES += VendorTagDescriptor.cpp

libcamera_client_la_CPPFLAGS := -I$(top_srcdir)/include
libcamera_client_la_CPPFLAGS += -std=c++11
libcamera_client_la_LDFLAGS  := -lutils

# Export headers
camera_client_includedir=$(includedir)/camera
camera_client_include_HEADERS := $(top_srcdir)/include/camera/CameraMetadata.h
camera_client_include_HEADERS += $(top_srcdir)/include/camera/CameraParameters.h

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libcamera_client.pc
EXTRA_DIST = $(pkgconfig_DATA)