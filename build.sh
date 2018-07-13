mkdir out 2>/dev/null
args="-framework CoreFoundation"
#args="Advapi32.lib"
$CXX --std=c++14 dyn_lib.cpp dump_icds.cpp find_icds.cpp tjson_cpp/tjson.cpp utils.cpp -o out/dump_icds $args $@
