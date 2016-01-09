#DEST_HOST="192.168.2.34" #pi-01
#DEST_HOST="192.168.2.186" #pi-02
#DEST_HOST="192.168.2.104" #pi-03
#DEST_HOST="192.168.2.156" #pi-04
DEST_HOST="192.168.2.129" #pi-05

DEST_DIR="~/miner"

echo Copy MinerTest to $DEST_HOST:$DEST_DIR
scp -r MinerTest pi@$DEST_HOST:$DEST_DIR

#01: scp -r MinerTest pi@192.168.2.34:~/miner
#02: scp -r MinerTest pi@192.168.2.186:~/miner
#03: scp -r MinerTest pi@192.168.2.104:~/miner
#04: scp -r MinerTest pi@192.168.2.156:~/miner
#05: scp -r MinerTest pi@192.168.2.129:~/miner
