# CPE333 Mini-Project 2: Process Scheduler Profiler

Linux kernel module สำหรับติดตามการจัดตารางเวลาของ process ทีละตัว module นี้นับจำนวนครั้งที่ scheduler สลับ process เข้าและออกจาก CPU (แยก voluntary กับ involuntary) จำนวนครั้งที่ process ตื่นจาก sleep และจำนวนครั้งที่ process ย้าย CPU โดยดักจาก tracepoint `sched_switch`, `sched_wakeup` และ `sched_migrate_task` แล้วแสดงผลที่ `/proc/sched_profiler`

งานนี้ทดสอบบน Ubuntu 24.04.5 ใน VMware VM `CPE333-kernel` (12 vCPU) ที่บูต custom kernel `7.0.0-999-generic` จาก Mini-Project 1 โจทย์อยู่ใน [`mini_project_2_cpu_virtualization.md`](mini_project_2_cpu_virtualization.md)

## โครงสร้าง

| Path | เนื้อหา |
|---|---|
| `kernel/sched_profiler.c` | Kernel module (probe ของ tracepoint ทั้งสามตัว, `/proc/sched_profiler`, module parameter `pid`) |
| `kernel/intree_patch.sh`, `kernel/intree_build.sh` | นำ module เข้า `drivers/misc/` ของ kernel source tree แล้ว rebuild แบบ incremental |
| `kernel/in_tree_integration.md` | ขั้นตอน in-tree build ทั้งหมด รวมถึงปัญหา BTF mismatch และวิธีแก้ |
| `workloads/` | Program ทดสอบ 3 แบบ (`cpu_workload`, `sleep_workload`, `migrate_workload`) และ CLI `sched_profiler_ctl` |
| `scripts/` | `check_env.sh`, `run_demo.sh`, `validate_perf.sh` (เทียบกับ `perf record -a`), `validate_ftrace.sh` (เทียบกับ tracefs) |
| `result/screenshots/` | ภาพหลักฐาน `m01`–`m16` จากการรันจริงบน VM |
| `docs/MiniProject2_Report.docx` | รายงานฉบับส่ง |
| `docs/COMMANDS.md` | คำสั่งที่ใช้ถ่ายภาพแต่ละภาพตามลำดับ |
| `docs/THEORY_AND_REPORT_GUIDE.md`, `docs/PRESENTATION_PLAN.md` | ทฤษฎีที่ใช้อ้างในรายงานและแผนการนำเสนอ |

## วิธีใช้

Kbuild มีปัญหากับ timestamp และ symlink บน shared folder ของ VMware (`vmhgfs`) จึงต้องคัดลอกมา compile บน ext4 ของ VM ก่อน

```bash
cp -r /mnt/hgfs/Miniproj2 ~/miniproj2 && cd ~/miniproj2
make check                      # ตรวจ kernel config, headers และ perf
make all                        # build workloads และ kernel/sched_profiler.ko
sudo insmod kernel/sched_profiler.ko

# รัน program ทดสอบภายใต้ profiler (CLI ผูก PID ให้ก่อน exec)
./workloads/sched_profiler_ctl --run ./workloads/sleep_workload 10 20
./workloads/sched_profiler_ctl --run ./workloads/cpu_workload 4 --compete

# หรือสั่งผ่าน /proc โดยตรง
echo "pid 1234" > /proc/sched_profiler    # คำสั่งอื่น: tgid <PID>, reset, stop
cat /proc/sched_profiler

make validate-perf              # เทียบตัวเลขกับ perf
make validate-ftrace            # เทียบตัวเลขกับ ftrace
sudo rmmod sched_profiler
```

`make help` แสดง target ทั้งหมด ส่วน `/usr/bin/perf` จะไม่ยอมรันบน kernel `7.0.0-999` จึงต้องติดตั้ง `linux-tools-7.0.0-31-generic` แล้ว `validate_perf.sh` จะเลือก `perf` จาก `/usr/lib/linux-tools/` ให้เอง

## ผลการทดลอง

| ภาพ | การทดสอบ | ผล |
|---|---|---|
| `m06` | `cpu_workload 4 --compete` | involuntary 683 เท่ากับ `nivcsw` 683 จาก `getrusage` |
| `m07` | `sleep_workload 40 50` | Woken Up 41, voluntary 41 และ `nvcsw` 41 (sleep 40 รอบ บวกหนึ่งครั้งที่ขอบช่วงเวลาการนับ) |
| `m08` | `migrate_workload 25 40 --mode affinity` | CPU Migrations 25 เท่ากับ `se.nr_migrations` 25 |
| `m10`, `m11` | `validate_perf.sh`, `validate_ftrace.sh` | ต่างจาก perf และ ftrace เป็น 0 ในทุก metric ของทั้งสาม workload |
| `m14` | In-tree rebuild | `build exit status = 0` ใช้เวลา 12 นาที 43 วินาที compile 13 objects |
| `m16` | `modprobe` module แบบ in-tree | โหลดได้หลังตัด `.BTF` และ sign ด้วย build key ของ kernel |
