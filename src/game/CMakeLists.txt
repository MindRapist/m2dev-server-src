file(GLOB_RECURSE GAME_SOURCES "*.h" "*.c" "*.cpp")

add_executable(game ${GAME_SOURCES})

target_compile_definitions(game PRIVATE GIT_DESCRIBE="${GIT_DESCRIBE_VERSION}")

if (NOT WIN32 AND USE_SIL)
	set(CRYPTOPP_LINK_NAME cryptopp)
	target_include_directories(game PRIVATE /usr/local/include/cryptopp)
	target_link_options(game PRIVATE "-L/usr/local/lib")
else()
	set(CRYPTOPP_LINK_NAME cryptopp-static)
endif()

target_link_libraries(game 
	common 
	libgame 
	libpoly 
	libsql 
	libthecore 
	liblua

	# external
	mariadbclient

	# CRYPTOPP LOGIC
	${CRYPTOPP_LINK_NAME}
)

if (WIN32)
	target_link_libraries(game ws2_32)
else()
	target_link_libraries(game pthread md)
endif()
