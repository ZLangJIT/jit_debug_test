cmake_minimum_required(VERSION 3.20.0)
project(Jit)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

add_executable(jit jit.cpp main.cpp)

# Link against all LLVM libraries

message(STATUS "LLVM_AVAILABLE_LIBS = [ ${LLVM_AVAILABLE_LIBS} ]")

target_link_libraries(jit ${LLVM_AVAILABLE_LIBS})

set(INSTALL_BIN_DIR "${CMAKE_INSTALL_PREFIX}/bin" CACHE PATH "Installation directory for executables")
set(INSTALL_LIB_DIR "${CMAKE_INSTALL_PREFIX}/lib" CACHE PATH "Installation directory for libraries")
set(INSTALL_INC_DIR "${CMAKE_INSTALL_PREFIX}/include" CACHE PATH "Installation directory for headers")

install(TARGETS jit
        RUNTIME DESTINATION "${INSTALL_BIN_DIR}"
        ARCHIVE DESTINATION "${INSTALL_LIB_DIR}"
        LIBRARY DESTINATION "${INSTALL_LIB_DIR}" )

if (WIN32)
    install(FILES $<TARGET_PDB_FILE:jit> DESTINATION "${INSTALL_BIN_DIR}" OPTIONAL)
endif()

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/jit.h DESTINATION "${INSTALL_INC_DIR}")