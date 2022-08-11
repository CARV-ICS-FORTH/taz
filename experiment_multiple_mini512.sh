
TRACE_FILE=tests/apps/miniAero_512/trace.index
ARGS=${TRACE_FILE}
for I in  1 2 3 4 5 6 7 8; do
	time ./ilmm  $ARGS  > res_512_${I}.log
	ARGS="$ARGS ${TRACE_FILE}"
done
