
#!/bin/sh
set -e
make -f ./Makefile || exit
lcov -b . -d . -z > /dev/null
cpptests/gtest.sh || exit
gcov -c -b ./CMakeFiles/hiredis.dir/*.*  > /dev/null 2>&1
lcov -b ./ -c -d . --output-file initial_coverage.info --no-external > /dev/null 2>&1
lcov -r initial_coverage.info "*gtest*" "*cpptests*" "*adapter*" -o final_coverage.info > /dev/null 2>&1
lcov -r final_coverage.info "*sds*" "*net*" "*read*" "*dict*" "*sslio*" -o final_coverage.info > /dev/null 2>&1
lcov -l final_coverage.info
genhtml final_coverage.info --output-directory coverage > /dev/null 2>&1
#xdg-open coverage/index.html 