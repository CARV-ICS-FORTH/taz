#!/bin/bash
#SBATCH --partition=Workstations
#SBATCH --job-name=ilmm_mapping_experiment # Job name
#SBATCH --nodes=1                   # Use one node
#SBATCH --ntasks=1                  # Run a single task
#SBATCH --mem-per-cpu=20gb           # Memory per processor
#SBATCH --array=0-29
#SBATCH --output=/home/chaix/deep-sea/fast-replay/logs/ilmm_mapping_experiment_%A_%a.out
#SBATCH --error=/home/chaix/deep-sea/fast-replay/logs/ilmm_mapping_experiment_%A_%a.error

#    SLURM_ARRAY_TASK_ID=12

TARGETS=(0 0.5 0.9 0.99 0.999)
NODE_MTBF_LOG10_RANGE=(9 10 11 12 13 14)
#DURATION=$(($SLURM_ARRAY_TASK_ID * 3600 )) 
DURATION=$(( 100 * 3600 )) #100 hours 
pwd;hostname;date


export LANG=C
export WORKSPACE=$(pwd)

TARGET_INDEX=$(( $SLURM_ARRAY_TASK_ID % 5 ))
MTBF_INDEX=$(( $SLURM_ARRAY_TASK_ID / 5 ))
TARGET=${TARGETS[$TARGET_INDEX]}
NODE_MTBF=${NODE_MTBF_LOG10_RANGE[$MTBF_INDEX]}
LINK_MTBF=$(( $NODE_MTBF + 3 ))

STATS_PATH="${WORKSPACE}/results/mapping_experiment_t${TARGET}_m${NODE_MTBF}/"
#STATS_PATH="${WORKSPACE}/results/mapping_experiment_${DURATION}/"


echo "Run with for target ${TARGET} and MTBF ${NODE_MTBF}.."
mkdir -p ${STATS_PATH} 


#--cfg=smpi/alltoall:basic_linear 
#ulimit -Sn $((N * 2))
#gdb --args 
time ${WORKSPACE}/ilmm -t MEDIUM  -c @tests/conf/apps_stretched.conf                   \
	--inhibit_verbose_messages=1 --inhibit_debug_messages=1                        \
	--mapping_reliability_target=${TARGET}                                         \
       	-e ${DURATION} --warmup_period=10000 --drain_period=100000                     \
	--node_nominal_mtbf_log10=${NODE_MTBF}  --link_nominal_mtbf_log10=${LINK_MTBF} \
	--snapshots_type=-1 --snapshots_format=pydict --snapshots_occurence=8           \
        --stats_folder=${STATS_PATH}

