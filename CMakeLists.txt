cmake_minimum_required(VERSION 3.15)
project(m2dev-server-src)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ASan support
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

#Use System Installed Libs
option(USE_SIL "Use the system's installed libraries (must exist locally)" OFF)

set(CMAKE_MODULE_PATH
	${CMAKE_MODULE_PATH}
	"${CMAKE_CURRENT_SOURCE_DIR}/buildtool"
)

set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/_install")

if(WIN32)
	set_property(GLOBAL PROPERTY USE_FOLDERS ON)
	
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	
	add_compile_options(
		$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>
		$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:preprocessor>
	)
	
	if (MSVC)
		add_compile_options(/MP)
	endif()
else()
	include_directories(${MARIADB_INCLUDE_DIR})
	
	add_compile_options("$<$<CXX_COMPILER_ID:GNU>:-finput-charset=UTF-8>")
endif()

add_compile_definitions(
  $<$<PLATFORM_ID:Windows>:OS_WINDOWS>
  $<$<PLATFORM_ID:FreeBSD>:OS_FREEBSD>
  $<$<PLATFORM_ID:Linux>:OS_LINUX>
  $<$<PLATFORM_ID:Darwin>:OS_MACOS>
)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Derive a version string from git for the db target
find_package(Git QUIET)

if(GIT_FOUND)
	execute_process(
		COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} describe --always --dirty --tags
		OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
		OUTPUT_STRIP_TRAILING_WHITESPACE
		ERROR_QUIET
	)
endif()

# ====================================================================
# [START] Submodule and Sparse Checkout Automation (New Block)
# ====================================================================

# Ensure this is a Git repository before proceeding
if (EXISTS "${CMAKE_SOURCE_DIR}/.git")
	message(STATUS "Checking and initializing core Git submodules...")

	# --- 1. SPDLG Check ---
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spdlog/.git")
		message(STATUS "  -> Initializing spdlog...")
		execute_process(
			COMMAND git submodule update --init vendor/spdlog
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE RESULT
		)
		if (RESULT)
			message(FATAL_ERROR "Failed to initialize spdlog.")
		endif()
	endif()

	# --- 2. MARIADB-CONNECTOR-C Check ---
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mariadb-connector-c/.git")
		message(STATUS "  -> Initializing mariadb-connector-c...")
		execute_process(
			COMMAND git submodule update --init vendor/mariadb-connector-c
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE RESULT
		)
		if (RESULT)
			message(FATAL_ERROR "Failed to initialize mariadb-connector-c.")
		endif()
	endif()

	# --- 3. CRYPTOPP Check ---
	# Checking for a core source file, as the CMakeLists.txt is local.
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/cryptopp/src/.git")
		message(STATUS "  -> Initializing cryptopp...")
		execute_process(
			COMMAND git submodule update --init vendor/cryptopp
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE RESULT
		)
		if (RESULT)
			message(FATAL_ERROR "Failed to initialize cryptopp.")
		endif()
	endif()

	# --- 4. STB Check (Sparse Submodule) ---
	# STB requires checking for the *entire* submodule directory, not just the sparse file yet.
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb/.git")
		message(STATUS "  -> Initializing STB...")
		execute_process(
			COMMAND git submodule update --init vendor/stb
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE RESULT
		)
		if (RESULT)
			message(FATAL_ERROR "Failed to initialize STB.")
		endif()
	endif()

	# --- 5. PCG-CPP Check (Sparse Submodule) ---
	if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/pcg-cpp/.git")
		message(STATUS "  -> Initializing PCG-CPP...")
		execute_process(
			COMMAND git submodule update --init vendor/pcg-cpp
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
			RESULT_VARIABLE RESULT
		)
		if (RESULT)
			message(FATAL_ERROR "Failed to initialize PCG-CPP.")
		endif()
	endif()

	message(STATUS "All core submodules are initialized.")

	# --- 2. Sparse Checkout: STB Headers ---
	# Repo: https://github.com/nothings/stb (Files needed: stb_image.h, stb_image_write.h)
	set(STB_MODULE_NAME vendor/stb)
	set(STB_SUBMODULE_PATH "${CMAKE_SOURCE_DIR}/.git/modules/${STB_MODULE_NAME}")

	# NOTE: Assume you ran 'git submodule add https://github.com/nothings/stb.git vendor/stb' ONCE.
	if (NOT EXISTS "${STB_SUBMODULE_PATH}/info/sparse-checkout")
		message(STATUS "Configuring sparse checkout for STB headers...")

		# 2a. Create the sparse-checkout file with the required files (in the .git directory)
		execute_process(
			COMMAND ${CMAKE_COMMAND} -E make_directory "${STB_SUBMODULE_PATH}/info"
			COMMAND ${CMAKE_COMMAND} -E echo "stb_image.h\nstb_image_write.h" > "${STB_SUBMODULE_PATH}/info/sparse-checkout"
			OUTPUT_VARIABLE DUMMY_OUTPUT # Suppress CMake 3.15 warning about command output
		)

		# 2b. Apply the sparse filter by running update again
		execute_process(
			COMMAND git submodule update ${STB_MODULE_NAME}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		)
	endif()

	# --- 3. Sparse Checkout: PCG-CPP Headers ---
	# Repo: https://github.com/imneme/pcg-cpp (Files needed: include/pcg_*.hpp)
	set(PCG_MODULE_NAME vendor/pcg-cpp)
	set(PCG_SUBMODULE_PATH "${CMAKE_SOURCE_DIR}/.git/modules/${PCG_MODULE_NAME}")

	# NOTE: Assume you ran 'git submodule add https://github.com/imneme/pcg-cpp.git vendor/pcg-cpp' ONCE.
	if (NOT EXISTS "${PCG_SUBMODULE_PATH}/info/sparse-checkout")
		message(STATUS "Configuring sparse checkout for PCG-CPP headers...")

		# Create the sparse-checkout file. We list the 'include/' directory.
		execute_process(
			COMMAND ${CMAKE_COMMAND} -E make_directory "${PCG_SUBMODULE_PATH}/info"
			COMMAND ${CMAKE_COMMAND} -E echo "include/" > "${PCG_SUBMODULE_PATH}/info/sparse-checkout"
			OUTPUT_VARIABLE DUMMY_OUTPUT # Suppress CMake 3.15 warning
		)
		
		# Apply the sparse filter by running update again
		execute_process(
			COMMAND git submodule update ${PCG_MODULE_NAME}
			WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
		)
	endif()

endif()
# ====================================================================
# [END] Submodule and Sparse Checkout Automation
# ====================================================================

if(NOT GIT_DESCRIBE_VERSION)
	if(GIT_FOUND)
		execute_process(
			COMMAND ${GIT_EXECUTABLE} -C ${CMAKE_SOURCE_DIR} rev-parse --short HEAD
			OUTPUT_VARIABLE GIT_DESCRIBE_VERSION
			OUTPUT_STRIP_TRAILING_WHITESPACE
			ERROR_QUIET
		)
	else()
		set(GIT_DESCRIBE_VERSION "unknown")
	endif()
endif()

if(NOT GIT_DESCRIBE_VERSION)
	set(GIT_DESCRIBE_VERSION "unknown")
endif()

# ASan flags
if(ENABLE_ASAN)
	if(MSVC)
		add_compile_options(/fsanitize=address)
		add_link_options(/fsanitize=address)
		add_definitions(-D_DISABLE_VECTOR_ANNOTATION)
		add_definitions(-D_DISABLE_STRING_ANNOTATION)
	else()
		add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
		add_link_options(-fsanitize=address)
	endif()
endif()

add_definitions(-DNOMINMAX)
add_definitions(-DWIN32_LEAN_AND_MEAN)

include_directories(src)
include_directories(include)

# MariDB includes
include_directories(${CMAKE_BINARY_DIR}/vendor/mariadb-connector-c/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/vendor/mariadb-connector-c/include)

add_subdirectory(src)
add_subdirectory(vendor)
