# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\TaxiAnalysis_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\TaxiAnalysis_autogen.dir\\ParseCache.txt"
  "TaxiAnalysis_autogen"
  )
endif()
