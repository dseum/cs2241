# cs2241

## Bench Machine

### DRAM

This depends on [lmbench](https://github.com/intel/lmbench). Note that depending on your system, you may need to refer to [this issue](https://github.com/intel/lmbench/issues/21#issuecomment-1453553790) to get it to compile.

```
sudo bin/x86_64-linux-gnu/lat_mem_rd -P 1 -N 10 512M 128 2> dram.out
```

### SSD

This depends on [fio](https://github.com/axboe/fio).

```sh
sudo fio --filename=/dev/nvme0n1p1 \
    --rw=randread --bs=4k --iodepth=16 --direct=1 \
    --numjobs=1 --loops=10 \
    --group_reporting --write_lat_log=ssd \
    --ioengine=io_uring
```
