
#!/bin/sh
set -e
rm -f ./CMakeFiles/hiredis.dir/*.gcda ./cpptests/CMakeFiles/hiredis.dir/*.gcda
make -f ./Makefile || exit
./cpptests/hiredis-gtest || exit
gcov -c -b ./CMakeFiles/hiredis.dir/*.* ./cpptests/CMakeFiles/hiredis.dir/*.* 
lcov -b ./ -c -d . --output-file main_coverage.info --no-external \
    --remove /home/ariel/redis/hiredis/adapters	/home/ariel/redis/hiredis/cpptests	\
    include/gtest	include/gtest/internal	src	
genhtml main_coverage.info --output-directory coverage
#xdg-open coverage/index.html 