# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

add_library(td_tdb OBJECT)
init_target(td_tdb)
add_library(tdesktop::td_tdb ALIAS td_tdb)

target_precompile_headers(td_tdb PRIVATE ${src_loc}/tdb/tdb_pch.h)
nice_target_sources(td_tdb ${src_loc}
PRIVATE
    tdb/tdb_instance.cpp
    tdb/tdb_instance.h
)

target_include_directories(td_tdb
PUBLIC
    ${src_loc}
)

target_link_libraries(td_tdb
PUBLIC
    desktop-app::lib_base
PRIVATE
    desktop-app::external_td
)
