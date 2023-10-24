set -e 

SCRIPTS_PATH=$( dirname $(realpath $BASH_SOURCE) )

echo "Script path is $SCRIPTS_PATH"

FOO=${NPROCS:=8}
FOO=${CMAKE_ARGS:="-DENABLE_SCOTCH=1 -DENABLE_FLATBUFFERS=1"}
FOO=${CTEST_ARGS:="-R taz --output-on-failure"}

${SCRIPTS_PATH}/run_quick_tests.sh

cd ${SCRIPTS_PATH}/..

#1: build type
#2: add heavy tests
#3: test label
function do_test () {
  echo "Run $3 tests for build $1"
  cmake -DADD_HEAVY_TESTS=$2 -DPROFILE_GENERATE_FBS=1 -DCMAKE_BUILD_TYPE=$1 ${CMAKE_ARGS} . &> "test_$3_$1.log"
  make -j${NPROCS}                                                 &>> "test_$3_$1.log"
#  ctest  -j${NPROCS} ${CTEST_ARGS}                                 &>> "test_$3_$1.log"
}

#Run the set of tests in increasingly strict conditions

do_test Release 1 heavy
do_test Debug   1 heavy
do_test Asan    1 heavy

#Run test with valgrind memory checker..
export TAZ_TEST_WRAP_COMMAND="valgrind --tool=memcheck --leak-check=full --show-leak-kinds=all --trace-children=yes --error-exitcode=2"  
#Valgind on heavy tests would be unreasonable, wouldnt it ?
do_test Release 0 memcheck
do_test Debug   0 memcheck

echo "Done with heavy tests"
