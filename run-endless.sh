#!/bin/bash

source ./run-conf.sh

# *** Endless miner execution loop ***
while [ 1 ]; do

echo `date` ": Miner started" >>log-starts.log
sudo ./MinerTest $GRID_CONF $POOL_CONF $LOG_CONF 2>&1 >log.log
echo `date` "Miner execution interrupted" >>log-starts.log

done

