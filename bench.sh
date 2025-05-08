fio --name=latency-test --filename=/dev/nvme0n1p1 \
    --rw=randread --bs=4k --iodepth=1 --direct=1 \
    --numjobs=1 --time_based --runtime=30s --group_reporting
