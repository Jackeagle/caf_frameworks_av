# ACLOCAL_AMFLAGS = -I m4

amr_local_srcdir = $(abs_top_srcdir)/media/libstagefright/codecs/amrwbenc

AM_CFLAGS  = -I$(abs_top_srcdir)/media/libstagefright/codecs/common/include
AM_CFLAGS += -I$(amr_local_srcdir)/inc 
AM_CFLAGS += -I$(abs_top_srcdir)/include/media
AM_CFLAGS += -I$(amr_local_srcdir)/../../include

AM_CFLAGS += -DLINUX

if GCC_ARMV5
		AM_CFLAGS += -DARM -DASM_OPT -DPV_CPU_ARCH_VERSION=5 -DPV_COMPILER=1
		AM_CCASFLAGS += -DARMV5 -DPV_CPU_ARCH_VERSION=5 -Isrc/asm/ARMV5E
endif

if GCC_ARMV7_NEON
		AM_CFLAGS += -DARM -DARMV7 -DASM_OPT -DPV_COMPILER=1
		AM_CCASFLAGS += -DARMV7 -Isrc/asm/ARMV7
endif


if COMPILE_AS_C
   AM_CFLAGS += -x c -std=c99
   libav_amrwbenc_la_LINK = $(LINK) $(libav_amrwbenc_la_LDFLAGS)
else
    libav_amrwbenc_la_LINK = $(CXXLINK) $(libav_amrwbenc_la_LDFLAGS)
endif

AM_CXXFLAGS = $(AM_CFLAGS)

amrwbencincludedir = $(includedir)/av-amrwbenc
amrwbencinclude_HEADERS = $(amr_local_srcdir)/../common/include/voAMRWB.h \
                          $(amr_local_srcdir)/../common/include/cmnMemory.h
lib_LTLIBRARIES = libav_amrwbenc.la

libav_amrwbenc_la_LDFLAGS = -avoid-version --no-undefined -Wl -export-symbols $(amr_local_srcdir)/codec_amrwbenc.sym
EXTRA_DIST = $(amr_local_srcdir)/codec_amrwbenc.sym


# Our sources to include.
# if COMPILE_AS_C
libav_amrwbenc_la_SOURCES = \
    				src/autocorr.c \
	    			src/az_isp.c \
	    			src/bits.c \
	    			src/c2t64fx.c \
	    			src/c4t64fx.c \
	    			src/convolve.c \
	    			src/cor_h_x.c \
	    			src/decim54.c \
	    			src/deemph.c \
	    			src/dtx.c \
	    			src/g_pitch.c \
	    			src/gpclip.c \
	    			src/homing.c \
	    			src/hp400.c \
	    			src/hp50.c \
	    			src/hp6k.c \
	    			src/hp_wsp.c \
	    			src/int_lpc.c \
	    			src/isp_az.c \
	    			src/isp_isf.c \
	    			src/lag_wind.c \
	    			src/levinson.c \
	    			src/log2.c \
	    			src/lp_dec2.c \
	    			src/math_op.c \
	    			src/oper_32b.c \
	    			src/p_med_ol.c \
	    			src/pit_shrp.c \
	    			src/pitch_f4.c \
	    			src/pred_lt4.c \
	    			src/preemph.c \
	    			src/q_gain2.c \
	    			src/q_pulse.c \
	    			src/qisf_ns.c \
	    			src/qpisf_2s.c \
	    			src/random.c \
	    			src/residu.c \
	    			src/scale.c \
	    			src/stream.c \
	    			src/syn_filt.c \
	    			src/updt_tar.c \
	    			src/util.c \
	    			src/voAMRWBEnc.c \
	    			src/voicefac.c \
	    			src/wb_vad.c \
	    			src/weight_a.c \
	    			src/mem_align.c
# endif

if GCC_ARMV5
libav_amrwbenc_la_SOURCES += \
            src/asm/ARMV5E/convolve_opt.s \
            src/asm/ARMV5E/cor_h_vec_opt.s \
            src/asm/ARMV5E/Deemph_32_opt.s \
            src/asm/ARMV5E/Dot_p_opt.s \
            src/asm/ARMV5E/Filt_6k_7k_opt.s \
            src/asm/ARMV5E/Norm_Corr_opt.s \
            src/asm/ARMV5E/pred_lt4_1_opt.s \
            src/asm/ARMV5E/residu_asm_opt.s \
            src/asm/ARMV5E/scale_sig_opt.s \
            src/asm/ARMV5E/Syn_filt_32_opt.s \
            src/asm/ARMV5E/syn_filt_opt.s    
endif

if GCC_ARMV7_NEON
libav_amrwbenc_la_SOURCES += \
            src/asm/ARMV7/convolve_neon.s \
            src/asm/ARMV7/cor_h_vec_neon.s \
            src/asm/ARMV7/Deemph_32_neon.s \
            src/asm/ARMV7/Dot_p_neon.s \
            src/asm/ARMV7/Filt_6k_7k_neon.s \
            src/asm/ARMV7/Norm_Corr_neon.s \
            src/asm/ARMV7/pred_lt4_1_neon.s \
            src/asm/ARMV7/residu_asm_neon.s \
            src/asm/ARMV7/scale_sig_neon.s \
            src/asm/ARMV7/Syn_filt_32_neon.s \
            src/asm/ARMV7/syn_filt_neon.s

    # LOCAL_CFLAGS_arm := -DARM -DARMV7 -DASM_OPT -mfpu=vfpv3
endif

# common source file for memory process
libav_amrwbenc_la_SOURCES += \
					  $(amr_local_srcdir)/../common/cmnMemory.c
					  
