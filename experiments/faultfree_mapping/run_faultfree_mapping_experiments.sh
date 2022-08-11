#!/bin/bash
#SBATCH --partition=Workstations
#SBATCH --job-name=taz_faultfree_mapping # Job name
#SBATCH --nodes=1                   # Use one node
#SBATCH --ntasks=1                  # Run a single task
#SBATCH --mem-per-cpu=100gb           # Memory per processor
#SBATCH --array=0-2
#SBATCH --output=/home/chaix/deep-sea/taz/logs/taz_faultfree_mapping_%A_%a.out
#SBATCH --error=/home/chaix/deep-sea/taz/logs/taz_faultfree_mapping_%A_%a.error

#    SLURM_ARRAY_TASK_ID=12

#DURATION=$(($SLURM_ARRAY_TASK_ID * 3600 )) 
DURATION=$(( 150 * 3600 )) #1000 hours 
#DURATION=$(( 900000 )) #250 hours 
pwd;hostname;date


MAPPING_OPTIONS=("--resource_mapper=linear" "--resource_mapper=traffic_uniform  --rm_traffic_scotch_nthreads=14"  "--resource_mapper=traffic  --rm_traffic_scotch_nthreads=14" )

export LANG=C
SCRIPT_PATH=$( dirname $BASH_SOURCE )


MAPPING_INDEX=0
#RANDOM_SEED=0
if [[ "x${SLURM_ARRAY_TASK_ID}" != "x" ]]; then 
	echo "Slurm mode, setting parameters from SLURM_ARRAY_TASK_ID=${SLURM_ARRAY_TASK_ID}"
	#https://stackoverflow.com/questions/56962129/how-to-get-original-location-of-script-used-for-slurm-job
	SCRIPT_PATH="$(pwd)" #$(scontrol show job $SLURM_JOBID | awk -F= '/Command=/{print $2}')
	MAPPING_INDEX=$(( $SLURM_ARRAY_TASK_ID % 3 ))
#	RANDOM_SEED=$(( $SLURM_ARRAY_TASK_ID / 5 ))
else 	if [[ $# -ge 1 ]]; then
		echo "Argument mode, setting parametters from ID ${1}"
		MTBF_INDEX=$(( ${1} % 3 ))
#		RANDOM_SEED=$(( ${1} / 3 ))
	else
		echo "Default mode, setting parameters to default"
	fi	
fi

export WORKSPACE="${SCRIPT_PATH}/../../"
echo "Workspace is ${WORKSPACE}"

WRAPPER="time"
if [[ $# -ge 2 ]]; then
	WRAPPER=$2
fi

MAPPING_ARGS=${MAPPING_OPTIONS[$MAPPING_INDEX]}

STATS_PATH="${WORKSPACE}/results/faultfree_mapping_experiment_m${MAPPING_INDEX}/"


echo "Run with for mapping strategy ${MAPPING_INDEX}.."
mkdir -p ${STATS_PATH} 


#--cfg=smpi/alltoall:basic_linear 
#ulimit -Sn $((N * 2))
${WRAPPER} ${WORKSPACE}/taz-simulate -t MEDIUM -c @tests/conf/apps_stretched.conf               \
	--inhibit_verbose_messages=1  --resource_scheduler="fcfs|backfilling" \
       	--inhibit_debug_messages=1                        \
       -e ${DURATION} --warmup_period=10000 --drain_period=10000                   \
	--node_nominal_mtbf_log10=100  --link_nominal_mtbf_log10=100 \
	--snapshots_type=-1 --snapshots_format=pydict --snapshots_occurence=8           \
       --stats_folder=${STATS_PATH} --stats_period=300  --rm_dump_job_mappings=1 ${MAPPING_OPTIONS}
