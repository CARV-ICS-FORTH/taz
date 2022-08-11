#!/bin/bash
#SBATCH --partition=Workstations
#SBATCH --job-name=taz_mapping_experiment_traffiv # Job name
#SBATCH --nodes=1                   # Use one node
#SBATCH --ntasks=1                  # Run a single task
#SBATCH --mem-per-cpu=100gb           # Memory per processor
#SBATCH --array=0-71
#SBATCH --output=/home/chaix/deep-sea/taz/logs/taz_mapping_experiment_traffic_%A_%a.out
#SBATCH --error=/home/chaix/deep-sea/taz/logs/taz_mapping_experiment_traffic_%A_%a.error

#    SLURM_ARRAY_TASK_ID=12


#Number of threads to use for traffic based mapping
N_SCOTCH_THREADS=8


NODE_MTBF_LOG10_RANGE=(9 10 11 12 13 14) 
MTBF_COUNT=6
#DURATION=$(($SLURM_ARRAY_TASK_ID * 3600 )) 
DURATION=$(( 150 * 3600 )) #100 hours 
pwd;hostname;date


MAPPING_OPTIONS=("--resource_mapper=linear" "--resource_mapper=linear_veto" "--resource_mapper=traffic_uniform"  "--resource_mapper=traffic" )
MAPPING_NAMES=("linear" "linearveto" "uniform" "traffic")
MAPPING_COUNT=4

export LANG=C
SCRIPT_PATH=$( dirname $BASH_SOURCE )

GLOBAL_INDEX=0
if [[ "x${SLURM_ARRAY_TASK_ID}" != "x" ]]; then 
	echo "Slurm mode, setting parameters from SLURM_ARRAY_TASK_ID=${SLURM_ARRAY_TASK_ID}"
	SCRIPT_PATH=$(pwd)
	GLOBAL_INDEX=$SLURM_ARRAY_TASK_ID 
else 	if [[ $# -ge 1 ]]; then
		echo "Argument mode, setting parametters from ID ${1}"
		GLOBAL_INDEX=$SLURM_ARRAY_TASK_ID 
	else
		echo "Default mode, setting parameters to default"
	fi	
fi

MAPPING_INDEX=$(( $GLOBAL_INDEX % $MAPPING_COUNT ))
MTBF_INDEX=$(( ( $GLOBAL_INDEX / $MAPPING_COUNT) % $MTBF_COUNT ))
RANDOM_SEED=$(( $GLOBAL_INDEX / ($MAPPING_COUNT * $MTBF_COUNT) ))

export WORKSPACE="${SCRIPT_PATH}/../../"
echo "Workspace is ${WORKSPACE}"

WRAPPER="time"
if [[ $# -ge 2 ]]; then
	WRAPPER=$2
fi

MAPPING_ARGS="${MAPPING_OPTIONS[$MAPPING_INDEX]} --rm_traffic_scotch_nthreads=${N_SCOTCH_THREADS}"
MAPPING_NAME="${MAPPING_NAMES[$MAPPING_INDEX]}"
NODE_MTBF=${NODE_MTBF_LOG10_RANGE[$MTBF_INDEX]}
LINK_MTBF=$(( $NODE_MTBF + 3 ))


if [[ RANDOM_SEED -gt 0 ]]; then
	MAPPING_ARGS="${MAPPING_ARGS} --rm_traffic_scotch_random_seed=${RANDOM_SEED}"
fi

STATS_PATH="${WORKSPACE}/results/mapping_experiment_map${MAPPING_NAME}_mtbf${NODE_MTBF}/seed${RANDOM_SEED}"

echo "Run with mapping ${MAPPING_NAME} and  MTBF ${NODE_MTBF} and seed ${RANDOM_SEED}.."
mkdir -p ${STATS_PATH} 


#--cfg=smpi/alltoall:basic_linear
#--rm_only=1 
#ulimit -Sn $((N * 2))
${WRAPPER} ${WORKSPACE}/taz-simulate -t MEDIUM  -c @tests/conf/apps_stretched.conf            \
	--collect_state_changes=1 --inhibit_verbose_messages=1 --inhibit_debug_messages=1     \
	--resource_scheduler="fcfs|backfilling"                                               \
        -e ${DURATION} --warmup_period=10000 --drain_period=10000                             \
	--node_nominal_mtbf_log10=${NODE_MTBF}  --link_nominal_mtbf_log10=${LINK_MTBF}        \
        --node_fault_random_seed=${RANDOM_SEED} --link_fault_random_seed=${RANDOM_SEED}       \
	--snapshots_type=-1 --snapshots_format=pydict --snapshots_occurence=8                 \
       --stats_folder=${STATS_PATH} --stats_period=300 --rm_dump_job_mappings=1               \
       ${MAPPING_ARGS} 
