#!/bin/bash
rm -rf /var/log/mxc/memfabric_hybrid/*
rm -rf /root/ascend/log/debug/plog/*
source /usr/local/Ascend/ascend-toolkit/set_env.sh
source /usr/local/mxc/memfabric_hybrid/set_env.sh
export LD_LIBRARY_PATH=/usr/local/Ascend/ascend-toolkit/latest/python/site-packages/:$LD_LIBRARY_PATH
export MMC_LOCAL_CONFIG_PATH=$PWD/mmc-local-write.conf
export ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7
export ASCEND_BUFFER_POOL=4:8
export MC_ALLOC_SAME_NODE_FIRST=1
export HCCL_INTRA_ROCE_ENABLE=1
export HCCL_INTRA_PCIE_ENABLE=0
nohup python3 write_mutil_process.py &> write.log 2>&1 &
# python3 write_mutil_process.py

