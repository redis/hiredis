# Wind River Workbench generated Makefile.
# Do not edit!!!
#
# The file ".wrmakefile" is the template used by the Wind River Workbench to
# generate the makefiles of this project. Add user-specific build targets and
# make rules only(!) in this project's ".wrmakefile" file. These will then be
# automatically dumped into the makefiles.

WIND_HOME := $(subst \,/,$(WIND_HOME))
WIND_BASE := $(subst \,/,$(WIND_BASE))
WIND_USR := $(subst \,/,$(WIND_USR))
WRVX_COMPBASE := $(subst \,/,$(WRVX_COMPBASE))
WIND_KRNL_MK := $(subst \,/,$(WIND_KRNL_MK))
WIND_USR_MK := $(subst \,/,$(WIND_USR_MK))
WIND_BUILD := $(subst \,/,$(WIND_BUILD))
WIND_BUILD_TOOL := $(subst \,/,$(WIND_BUILD_TOOL))

all : check_vsb pre_build main_all post_build

_clean ::
	@echo "make: removing targets and objects of `pwd`"

TRACE=0
TRACEON=$(TRACE:0=@)
TRACE_FLAG=$(TRACEON:1=)

JOBS?=1
TARGET_JOBS?=$(JOBS)

MAKEFILE := Makefile

FLEXIBLE_BUILD := 1

BUILD_SPEC = vxw7_ge_rtm_e_BSP_ARMARCH8Allvm_LP64_LARGE_SMP
DEBUG_MODE = 1
ifeq ($(DEBUG_MODE),1)
MODE_DIR := Debug
else
MODE_DIR := NonDebug
endif
OBJ_DIR := .
WS_ROOT_DIR := C:/Git/csp-vxworks-rtm/WindRiver/workbench-4/workspace
PRJ_ROOT_DIR := C:/Users/502710851/Documents/GitHub/hiredis/vxworks-7



#Global Build Macros
PROJECT_TYPE = DKM
DEFINES = 
EXPAND_DBG = 0
APICHECK_OPTIONS = -warning


#BuildSpec specific Build Macros
VX_CPU_FAMILY = arm
CPU = ARMARCH8A
TOOL_FAMILY = llvm
TOOL_VERSION = llvm_2016_04
_CC_VERSION = llvm_3_8_1_1
TOOL = llvm
TOOL_PATH = $(WIND_HOME)/compilers/llvm-3.8.1.1/$(WIND_VX7_LLVM_HOST_TYPE)/bin
CC_ARCH_SPEC = --target=arm64 -ftls-model=local-exec -ffixed-x18 -mno-implicit-float 
VSB_DIR = $(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP
VSB_CONFIG_FILE = $(VSB_DIR)/h/config/vsbConfig.h
LIBPATH = 
LIBS = 
DKM_LONGCALL = 
APICHECK_CMD = apicheck.sh -r "$(VSB_DIR)/krnl/apicheck/forbidden_api.lst" -n "nmarm64" -u -s "$<" "$@" $(APICHECK_OPTIONS)
APICHECK = 0

IDE_INCLUDES = -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/share/h -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/krnl/h/system -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/krnl/h/public -IC:/Users/502710851/Documents/GitHub/hiredis/include 

IDE_LIBRARIES = 

IDE_DEFINES = -DCPU=_VX_$(CPU) -DTOOL_FAMILY=$(TOOL_FAMILY) -DTOOL=$(TOOL) -D_WRS_KERNEL -D_WRS_VX_SMP -D_WRS_CONFIG_SMP -D_VSB_CONFIG_FILE=\"$(VSB_DIR)/h/config/vsbConfig.h\" -DARMEL  -D_HAVE_TOOL_XTORS -fno-use-init-array -D__ELF__ -U__USER_LABEL_PREFIX__ -D__USER_LABEL_PREFIX__= -DARM_USE_VFP 



#BuildTool flags
ifeq ($(DEBUG_MODE),1)
DEBUGFLAGS_C-Compiler = -g
DEBUGFLAGS_C++-Compiler = -g
DEBUGFLAGS_Linker = -g
DEBUGFLAGS_Partial-Image-Linker = 
DEBUGFLAGS_Librarian = 
DEBUGFLAGS_Assembler = -g
else
DEBUGFLAGS_C-Compiler = -O2
DEBUGFLAGS_C++-Compiler = -O2
DEBUGFLAGS_Linker = -O2
DEBUGFLAGS_Partial-Image-Linker = 
DEBUGFLAGS_Librarian = 
DEBUGFLAGS_Assembler = -O2
endif


#Project Targets
PROJECT_TARGETS = libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static.a


#Rules

# libcsp_hiredis_static
ifeq ($(DEBUG_MODE),1)
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_C-Compiler = -g
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_C++-Compiler = -g
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Linker = -g
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Partial-Image-Linker = 
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Librarian = 
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Assembler = -g
else
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_C-Compiler = -O2
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_C++-Compiler = -O2
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Linker = -O2
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Partial-Image-Linker = 
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Librarian = 
libcsp_hiredis_static/$(MODE_DIR)/% : DEBUGFLAGS_Assembler = -O2
endif
libcsp_hiredis_static/$(MODE_DIR)/% : IDE_INCLUDES = -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/share/h -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/krnl/h/system -I$(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP/krnl/h/public -IC:/Users/502710851/Documents/GitHub/hiredis/include 
libcsp_hiredis_static/$(MODE_DIR)/% : IDE_LIBRARIES = 
libcsp_hiredis_static/$(MODE_DIR)/% : IDE_DEFINES = -DCPU=_VX_$(CPU) -DTOOL_FAMILY=$(TOOL_FAMILY) -DTOOL=$(TOOL) -D_WRS_KERNEL -D_WRS_VX_SMP -D_WRS_CONFIG_SMP -D_VSB_CONFIG_FILE=\"$(VSB_DIR)/h/config/vsbConfig.h\" -DARMEL  -D_HAVE_TOOL_XTORS -fno-use-init-array -D__ELF__ -U__USER_LABEL_PREFIX__ -D__USER_LABEL_PREFIX__= -DARM_USE_VFP 
libcsp_hiredis_static/$(MODE_DIR)/% : PROJECT_TYPE = DKM
libcsp_hiredis_static/$(MODE_DIR)/% : DEFINES = 
libcsp_hiredis_static/$(MODE_DIR)/% : EXPAND_DBG = 0
libcsp_hiredis_static/$(MODE_DIR)/% : APICHECK_OPTIONS = -warning
libcsp_hiredis_static/$(MODE_DIR)/% : VX_CPU_FAMILY = arm
libcsp_hiredis_static/$(MODE_DIR)/% : CPU = ARMARCH8A
libcsp_hiredis_static/$(MODE_DIR)/% : TOOL_FAMILY = llvm
libcsp_hiredis_static/$(MODE_DIR)/% : TOOL_VERSION = llvm_2016_04
libcsp_hiredis_static/$(MODE_DIR)/% : _CC_VERSION = llvm_3_8_1_1
libcsp_hiredis_static/$(MODE_DIR)/% : TOOL = llvm
libcsp_hiredis_static/$(MODE_DIR)/% : TOOL_PATH = $(WIND_HOME)/compilers/llvm-3.8.1.1/$(WIND_VX7_LLVM_HOST_TYPE)/bin
libcsp_hiredis_static/$(MODE_DIR)/% : CC_ARCH_SPEC = --target=arm64 -ftls-model=local-exec -ffixed-x18 -mno-implicit-float 
libcsp_hiredis_static/$(MODE_DIR)/% : VSB_DIR = $(WS_ROOT_DIR)/vxw7_ge_rtm_e_BSP
libcsp_hiredis_static/$(MODE_DIR)/% : VSB_CONFIG_FILE = $(VSB_DIR)/h/config/vsbConfig.h
libcsp_hiredis_static/$(MODE_DIR)/% : LIBPATH = 
libcsp_hiredis_static/$(MODE_DIR)/% : LIBS = 
libcsp_hiredis_static/$(MODE_DIR)/% : DKM_LONGCALL = 
libcsp_hiredis_static/$(MODE_DIR)/% : APICHECK_CMD = apicheck.sh -r "$(VSB_DIR)/krnl/apicheck/forbidden_api.lst" -n "nmarm64" -u -s "$<" "$@" $(APICHECK_OPTIONS)
libcsp_hiredis_static/$(MODE_DIR)/% : APICHECK = 0
libcsp_hiredis_static/$(MODE_DIR)/% : OBJ_DIR := libcsp_hiredis_static/$(MODE_DIR)

libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/async.o : C:/Users/502710851/Documents/GitHub/hiredis/src/async.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/dict.o : C:/Users/502710851/Documents/GitHub/hiredis/src/dict.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/hiredis.o : C:/Users/502710851/Documents/GitHub/hiredis/src/hiredis.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/net.o : C:/Users/502710851/Documents/GitHub/hiredis/src/net.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/read.o : C:/Users/502710851/Documents/GitHub/hiredis/src/read.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/sds.o : C:/Users/502710851/Documents/GitHub/hiredis/src/sds.c $(FORCE_FILE_BUILD)
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ccllvm $(DEBUGFLAGS_C-Compiler) $(CC_ARCH_SPEC)  -D__vxworks -D__VXWORKS__ -nostdlibinc -nostdinc++ -fno-zero-initialized-in-bss -Wall     $(DKM_LONGCALL) $(IDE_DEFINES) $(DEFINES) $(ADDED_CFLAGS) $(IDE_INCLUDES) $(ADDED_INCLUDES) -o "$@" -c "$<" && if [ "$(APICHECK)" = "1" ]; then $(APICHECK_CMD);fi


OBJECTS_libcsp_hiredis_static = libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/async.o \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/dict.o \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/hiredis.o \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/net.o \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/read.o \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/sds.o

LibrarianObject.list: $(OBJECTS_libcsp_hiredis_static)
	$(file >$@,$^)
ifeq ($(TARGET_JOBS),1)
libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static.a : LibrarianObject.list | check_objdirs
	$(TRACE_FLAG)if [ ! -d "`dirname "$@"`" ]; then mkdir -p "`dirname "$@"`"; fi;echo "building $@"; $(TOOL_PATH)/ararm64 crus "$@" @LibrarianObject.list $(ADDED_OBJECTS)

else
libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static.a : libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static.a_jobs

endif
libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static_compile_file : $(FILE) ;

_clean :: libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static_clean

libcsp_hiredis_static/$(MODE_DIR)/libcsp_hiredis_static_clean : 
	$(TRACE_FLAG)if [ -d "libcsp_hiredis_static" ]; then cd "libcsp_hiredis_static"; rm -rf $(MODE_DIR); fi

force : 

TARGET_JOBS_RULE?=echo "Update the makefile template via File > Import > Build Settings : Update makefile template";exit 1
%_jobs : 
	$(TRACE_FLAG)$(TARGET_JOBS_RULE)

DEP_FILES := libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/async.d libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/dict.d libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/hiredis.d \
	 libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/net.d libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/read.d libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src/sds.d
-include $(DEP_FILES)

OBJ_DIRS := libcsp_hiredis_static/$(MODE_DIR)/Objects/csp_hiredis_static/src
check_objdirs : 
	$(TRACE_FLAG)echo "Creating object dirs";\
	for dir in $(OBJ_DIRS); do\
		if [ ! -d "$$dir" ]; then mkdir -p "$$dir"; fi;\
	done


-include $(PRJ_ROOT_DIR)/*.makefile

-include *.makefile

TARGET_JOBS_RULE=$(MAKE) -f $(MAKEFILE) --jobs $(TARGET_JOBS) $(MFLAGS) $* TARGET_JOBS=1
ifeq ($(JOBS),1)
main_all : external_build $(PROJECT_TARGETS)
	@echo "make: built targets of `pwd`"
else
main_all : external_build 
	@$(MAKE) -f $(MAKEFILE) --jobs $(JOBS) $(MFLAGS) $(PROJECT_TARGETS) TARGET_JOBS=1 &&\
	echo "make: built targets of `pwd`"
endif 

check_vsb :
	@if [ "$(SKIP_VSB_CHECK)" = "" ]; then\
		if [ -f $(WIND_BASE)/build/mk/scripts/check_vsb.sh ]; then\
			jobsoption="";\
			if [ "$(JOBS)" != "" ]; then\
				jobsoption="-j $(JOBS)";\
			fi;\
			sh $(WIND_BASE)/build/mk/scripts/check_vsb.sh $$jobsoption $(VSB_DIR);\
		fi;\
	fi

# entry point for extending the build
external_build ::
	@echo ""

# main entry point for pre processing prior to the build
pre_build :: check_objdirs $(PRE_BUILD_STEP) generate_sources
	@echo ""

# entry point for generating sources prior to the build
generate_sources ::
	@echo ""

# main entry point for post processing after the build
post_build :: $(POST_BUILD_STEP) deploy_output
	@echo ""

# entry point for deploying output after the build
deploy_output ::
	@echo ""

clean :: external_clean $(CLEAN_STEP) _clean

# entry point for extending the build clean
external_clean ::
	@echo ""
