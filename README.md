# DistributedIntegral
Launch
```
docker-compose up --build --scale worker=3
```
Instead of 'worker=3' you can use any amount of workers < 20

```
docker exec -it distributedintegral-master-1 /bin/bash
tc qdisc add dev eth0 root netem delay 300ms
tc qdisc add dev eth0 root netem loss 30%
tc qdisc add dev eth0 root netem duplicate 30%
```

```
docker exec -it distributedintegral-worker-1 /bin/bash
tc qdisc add dev eth0 root netem loss 100%
tc qdisc change dev eth0 root netem loss 0%
```
