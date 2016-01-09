#!/bin/sh

HOSTNAME=`hostname`

GRID_CONF="-spi_cnt 6 -chain_len 78"

POOL_CONF="-host pool.cloudhashing.com -port 3333 -user bf_$HOSTNAME"

LOG_CONF="-log_delay 100 -spi_f 6 -spi_f2 2 -spi_reset 6"
