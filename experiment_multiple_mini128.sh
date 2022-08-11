
TRACE_FILE=tests/apps/miniAero_128/trace.index
ARGS=${TRACE_FILE}
for I in  `seq 1 32`; do
	time ./ilmm  $ARGS  > res_128_${I}.log
	ARGS="$ARGS ${TRACE_FILE}"
done
