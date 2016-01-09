echo "*** Script processes ***"
ps -ef | grep run-endless.sh | grep -v grep

echo "*** Miner processes ***"
ps -ef | grep MinerTest | grep -v grep
