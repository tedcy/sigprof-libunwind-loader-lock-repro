cmake_minimum_required(VERSION 3.16)
project(minimal_gperftools_libunwind_helper_deps C)

include(ExternalProject)

set(GPERFTOOLS_VERSION 2.7)
set(LIBUNWIND_VERSION 1.3.1)

set(DEPS_ROOT ${CMAKE_CURRENT_LIST_DIR})
set(DOWNLOAD_DIR ${DEPS_ROOT}/downloads)
set(SRC_DIR ${DEPS_ROOT}/src)
set(INSTALL_DIR ${CMAKE_INSTALL_PREFIX})

file(MAKE_DIRECTORY ${DOWNLOAD_DIR} ${SRC_DIR} ${INSTALL_DIR})

set(LIBUNWIND_URL
    https://github.com/libunwind/libunwind/releases/download/v${LIBUNWIND_VERSION}/libunwind-${LIBUNWIND_VERSION}.tar.gz)
set(GPERFTOOLS_URL
    https://github.com/gperftools/gperftools/releases/download/gperftools-${GPERFTOOLS_VERSION}/gperftools-${GPERFTOOLS_VERSION}.tar.gz)
set(HELPER_URL
    https://github.com/alk/unwind_safeness_helper/archive/refs/heads/master.tar.gz)

ExternalProject_Add(libunwind
    URL ${LIBUNWIND_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${SRC_DIR}/libunwind-${LIBUNWIND_VERSION}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --prefix=${INSTALL_DIR}
        --enable-shared
        --disable-static
        --disable-minidebuginfo
        CFLAGS=-O2\ -g\ -fPIC\ -fcommon\ -U_FORTIFY_SOURCE
    BUILD_COMMAND $(MAKE)
    INSTALL_COMMAND $(MAKE) install
    BUILD_IN_SOURCE 1
)

ExternalProject_Add(gperftools
    URL ${GPERFTOOLS_URL}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${SRC_DIR}/gperftools-${GPERFTOOLS_VERSION}
    CONFIGURE_COMMAND <SOURCE_DIR>/configure
        --prefix=${INSTALL_DIR}
        --enable-libunwind
        --disable-shared
        --enable-static
        --with-pic
        CPPFLAGS=-I${INSTALL_DIR}/include
        LDFLAGS=-L${INSTALL_DIR}/lib
        LIBS=-lunwind
    BUILD_COMMAND $(MAKE) libprofiler.la
    INSTALL_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${INSTALL_DIR}/lib ${INSTALL_DIR}/include/gperftools
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/.libs/libprofiler.a ${INSTALL_DIR}/lib/libprofiler.a
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/src/gperftools/profiler.h ${INSTALL_DIR}/include/gperftools/profiler.h
    BUILD_IN_SOURCE 1
    DEPENDS libunwind
)

ExternalProject_Add(unwind_safeness_helper
    URL ${HELPER_URL}
    DOWNLOAD_NAME unwind_safeness_helper-patched.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${SRC_DIR}/unwind_safeness_helper-patched
    PATCH_COMMAND patch -p1 < ${DEPS_ROOT}/../patches/unwind-safeness-helper-local-only.patch
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${INSTALL_DIR}/lib
        COMMAND
        ${CMAKE_C_COMPILER} -O2 -g -fPIC -shared -DUNW_LOCAL_ONLY
        -I${INSTALL_DIR}/include
        -L${INSTALL_DIR}/lib
        -o ${INSTALL_DIR}/lib/unwind_safeness_helper.so
        <SOURCE_DIR>/unwind_safeness_helper.c
        -lunwind -ldl -pthread
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
    DEPENDS libunwind
)

ExternalProject_Add(unwind_safeness_helper_upstream
    URL ${HELPER_URL}
    DOWNLOAD_NAME unwind_safeness_helper-upstream.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    DOWNLOAD_DIR ${DOWNLOAD_DIR}
    SOURCE_DIR ${SRC_DIR}/unwind_safeness_helper-upstream
    CONFIGURE_COMMAND ""
    BUILD_COMMAND
        ${CMAKE_COMMAND} -E make_directory ${INSTALL_DIR}/lib
        COMMAND
        ${CMAKE_C_COMPILER} -O2 -g -fPIC -shared
        -I${INSTALL_DIR}/include
        -L${INSTALL_DIR}/lib
        -o ${INSTALL_DIR}/lib/unwind_safeness_helper_upstream.so
        <SOURCE_DIR>/unwind_safeness_helper.c
        -lunwind -ldl -pthread
    INSTALL_COMMAND ""
    BUILD_IN_SOURCE 1
    DEPENDS libunwind
)

add_custom_target(helper DEPENDS
    gperftools
    unwind_safeness_helper
    unwind_safeness_helper_upstream
)
