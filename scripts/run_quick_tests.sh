set -e 

SCRIPTS_PATH=$( dirname $(realpath $BASH_SOURCE) )

echo "Script path is $SCRIPTS_PATH"

FOO=${NPROCS:=8}
FOO=${CMAKE_ARGS:="-DENABLE_SCOTCH=1 -DENABLE_FLATBUFFERS=1"}
FOO=${CTEST_ARGS:="-R taz --output-on-failure"}

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

do_test Release 0 light
do_test Debug   0 light

echo "Done with quick tests"
