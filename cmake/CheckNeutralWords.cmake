# The space setting's words kept out of the engine's modules (docs/history/space-extraction.md): every
# word found in a module's headers and sources, outside its string literals (a saved or
# reflected name keeps its old words until a migration changes it), camel case and snake case split into words,
# has to be allowed for that file, and every allowance has to be still needed, so the list only
# shrinks. Run by the neutral_words test, or with -DWRITE=ON to write the list as the tree has it.
#
#   cmake -DENGINE=<source/engine> -DALLOWED=<list> [-DWRITE=ON] -P <this>

# The words, each a regular expression over lower case, and the name the list gives it.
set(words
	"star ?systems?=star-system"
	"celestial=celestial"
	"galax(y|ies)=galaxy"
	"jump ?(point|lane|gate)s?=jump-point"
	"stations?=station"
	"ships?=ship"
	"hulls?=hull"
	"cruis(e|es|ing)=cruise"
	"pads?=pad"
	"cargo=cargo")

file(GLOB modules LIST_DIRECTORIES true RELATIVE "${ENGINE}" "${ENGINE}/*")
set(found)
foreach(module IN LISTS modules)
	if(NOT IS_DIRECTORY "${ENGINE}/${module}")
		continue()
	endif()
	file(GLOB_RECURSE sources RELATIVE "${ENGINE}"
		"${ENGINE}/${module}/include/*.h" "${ENGINE}/${module}/src/*.h"
		"${ENGINE}/${module}/src/*.cpp")
	list(SORT sources)
	foreach(source IN LISTS sources)
		file(READ "${ENGINE}/${source}" text)
		string(REGEX REPLACE "\"[^\"\n]*\"" " " text "${text}")
		# Words, not identifiers: StationLayout and station_reach each hold "station".
		string(REGEX REPLACE "([a-z0-9])([A-Z])" "\\1 \\2" text "${text}")
		string(TOLOWER "${text}" text)
		string(REGEX REPLACE "[^a-z0-9]+" " " text " ${text} ")
		# Geometry's own term, not a vehicle's.
		string(REPLACE " convex hull " " " text "${text}")
		string(REPLACE " convex hulls " " " text "${text}")
		foreach(entry IN LISTS words)
			string(REGEX MATCH "^(.*)=(.*)$" parts "${entry}")
			set(pattern "${CMAKE_MATCH_1}")
			set(name "${CMAKE_MATCH_2}")
			if(text MATCHES " (${pattern}) ")
				list(APPEND found "${source} ${name}")
			endif()
		endforeach()
	endforeach()
endforeach()

if(WRITE)
	list(JOIN found "\n" lines)
	file(WRITE "${ALLOWED}" "# The space setting's words allowed in the engine's modules, a file and a word a line: none since\n# the space extraction (docs/history/space-extraction.md); cmake/CheckNeutralWords.cmake checks it.\n${lines}\n")
	return()
endif()

file(STRINGS "${ALLOWED}" allowed REGEX "^[^#]")
set(failures)
foreach(hit IN LISTS found)
	if(NOT hit IN_LIST allowed)
		list(APPEND failures "  ${hit}: not allowed; use the neutral term")
	endif()
endforeach()
foreach(allowance IN LISTS allowed)
	if(NOT allowance IN_LIST found)
		list(APPEND failures "  ${allowance}: allowed but no longer there; remove it from the list")
	endif()
endforeach()
if(failures)
	list(JOIN failures "\n" report)
	message(FATAL_ERROR "The neutral modules and ${ALLOWED} disagree:\n${report}")
endif()
