mkdir out 2>/dev/null
args="-framework CoreFoundation"
$CXX --std=c++14 dump_icds.cpp find_icds.cpp tjson_cpp/tjson.cpp utils.cpp -o out/dump_icds $args $@
