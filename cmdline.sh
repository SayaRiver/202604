#within ue docker container
apt-get update && apt-get install -y iproute2
apt-get install -y iputils-ping

ip route add 192.168.70.150/32 dev oaitun_ue1

ip route get 192.168.70.150

ping -c 3 192.168.70.150

apt install python3-paho-mqtt

python3 pubx.py 1
