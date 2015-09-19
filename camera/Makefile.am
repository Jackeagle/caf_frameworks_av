ACLOCAL_AMFLAGS = -I m4

## build libcamparams ##
lib_LTLIBRARIES = libcamparams.la

include_HEADERS = ../include/camera/CameraParameters.h

libcamparams_la_CXXFLAGS  = -fPIC
libcamparams_la_CXXFLAGS += -I ../include

libcamparams_la_SOURCES  = CameraParameters.cpp

libcamparams_la_LDFLAGS = -shared

