#!/bin/bash

set -e

cd m4ri-20140914
./configure
make -j4
cd ..

BASE=`pwd`
M4RI="${M4RI}m4ri-20140914"


mkdir -p build
cd build
rm -rf cm* CM* cmsat4-src Make*
cmake --version
cmake -DM4RI_ROOT_DIR=$M4RI ..
make -j4 VERBOSE=1
cd ..

SOLVER="`pwd`/build/cryptominisat"
cat <<EOF > solver
#!/bin/bash
${SOLVER} --threads \$2 \$1
EOF
chmod +x solver
