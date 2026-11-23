# Mini-Project 2 : คำสั่งที่ต้องทำใน VMware VM (`CPE333-kernel`) ตามลำดับ

คู่มือปฏิบัติงานภาคสนามสำหรับกลุ่ม ทำบน **VMware Workstation VM `CPE333-kernel`** (Ubuntu 24.04.5 LTS, 12 vCPU, Custom Kernel `7.0.0-999-generic` จาก Mini-Project 1)

> **ข้อสำคัญเรื่อง Shared Folder (`vmhgfs`):**
> โครงสร้าง Kbuild ของ Linux Kernel (`make modules`) สร้าง symlink และเช็ก timestamp ของไฟล์ `.o`/`.cmd` ซึ่งบางครั้งมีปัญหากับ VMware HGFS (`vmhgfs-fuse`) ให้แชร์โฟลเดอร์ `Miniproj2` เพื่อขนส่งไฟล์ แล้ว **`cp -r` มาคอมไพล์ในระบบไฟล์ ext4 ของ VM (`~/miniproj2`)** เสมอ

---

## รายการภาพหน้าจอหลักฐาน (`result/screenshots/`)

| รหัสภาพ | ชื่อไฟล์ | สิ่งที่ต้องเห็นในภาพ |
|---|---|---|
| `m01` | `m01_env_check.png` | ผลจาก `scripts/check_env.sh` เห็น `7.0.0-999-generic`, 12 CPUs, `CONFIG_TRACEPOINTS=y`, `CONFIG_MODULES=y` |
| `m02` | `m02_sched_h_proto.png` | Prototype ของ `sched_switch`, `sched_wakeup`, `sched_migrate_task` ใน `include/trace/events/sched.h` |
| `m03` | `m03_build.png` | ผล `make all` สำเร็จ (ได้ `kernel/sched_profiler.ko` และ 4 binaries ใน `workloads/` ไม่มี error/warning) |
| `m04` | `m04_insmod.png` | `sudo insmod kernel/sched_profiler.ko`, `lsmod | grep sched_profiler`, `dmesg | tail`, `cat /proc/sched_profiler` |
| `m05` | `m05_module_param.png` | ทดสอบรับ Input ทาง CLI ตอนโหลดโมดูล (`sudo insmod kernel/sched_profiler.ko pid=$$`) และ PID ที่ไม่มีอยู่ถูกปฏิเสธทั้งสองช่องทาง (`insmod ... pid=999999` → `Invalid parameters`, `echo 999999 > /proc/...` → `No such process`) |
| `m06` | `m06_cpu_workload.png` | Demo Workload 1 (`cpu_workload 4 --compete`) เห็น `Scheduled In / Out` และ `involuntary` สูง ตรงกับ `nivcsw` ในบรรทัด `self-check` |
| `m07` | `m07_sleep_workload.png` | Demo Workload 2 (`sleep_workload 40 50`) เห็น `Woken Up` และ `voluntary` เพิ่มตรงตามรอบ sleep |
| `m08` | `m08_migrate_workload.png` | Demo Workload 3A (`migrate_workload 25 40 --mode affinity`) เห็น `CPU Migrations` และ `Last Migration` |
| `m09` | `m09_taskset_migrate.png` | Demo Workload 3B ใช้ `taskset -cp` ย้าย Core ของโปรเซสที่กำลังทำงานอยู่ |
| `m10` | `m10_validate_perf.png` | ผลตารางเปรียบเทียบ `scripts/validate_perf.sh` (`sched_profiler` vs `perf record -a`) |
| `m11` | `m11_validate_ftrace.png` | ผลตารางเปรียบเทียบ `scripts/validate_ftrace.sh` (`sched_profiler` vs `ftrace`) |
| `m12` | `m12_rmmod_dmesg.png` | `sudo rmmod sched_profiler`, ตรวจ `/proc/sched_profiler` หายไปแล้ว และ `dmesg` ไม่มี Panic/Warning |
| `m13` | `m13_intree_patch.png` | In-tree: `diff -u` ของ `drivers/misc/Kconfig` (เพิ่ม `config SCHED_PROFILER`) และ `drivers/misc/Makefile` และ `CONFIG_SCHED_PROFILER=m` ใน `.config` ของ build tree |
| `m14` | `m14_intree_build.png` | In-tree: rebuild ด้วย `debian/rules` (`build exit status = 0`, 12m 43s, compile 13 objects) ได้ `sched_profiler.ko` ที่ `intree: Y`, `vermagic 7.0.0-999-generic` |
| `m15` | `m15_intree_btf_mismatch.png` | In-tree: `modprobe` ครั้งแรกได้ `Invalid argument` เพราะ BTF ของ module ไม่ตรงกับ kernel ที่รันอยู่ (`failed to validate module ... BTF: -22`, hash ของ BTF ต่างกัน) |
| `m16` | `m16_intree_modprobe.png` | In-tree: ตัด `.BTF`, sign ด้วย key ของ kernel, ติดตั้งใน `/lib/modules`, `modprobe` สำเร็จ, ทดสอบ workload และ `modprobe -r` |

> **สถานะ (23 พ.ย. 2569):** ภาพ `m01`–`m16` ถ่ายครบแล้วใน `result/screenshots/` ทุกภาพมาจากการรันจริงบน `7.0.0-999-generic` ด้วยสคริปต์ `tools/vm_evidence.sh` (ไม่อยู่ใน Git) ซึ่งรันคำสั่งใน Phase 2–7 ด้านล่างตามลำดับ หนึ่งหน้าจอต่อหนึ่งภาพ (`m13`–`m16` รันแยกด้วย `vm_evidence.sh m13 m14 m15 m16`)
> **ข้อควรรู้:** VMware Player ปิด Shared Folders เองทุกครั้งที่เปิดเครื่อง ถ้า `/mnt/hgfs/Miniproj2` หายไป ให้เปิดใน Player (Manage → Virtual Machine Settings → Options → Shared Folders → Always enabled) แล้วรัน `sudo mount -a` ใน VM

---

## Phase 1 — เตรียมโฟลเดอร์งานใน VMware VM (`CPE333-kernel`)

1. เปิด VMware Workstation Player → เลือก `CPE333-kernel` → **Manage → Virtual Machine Settings → Options → Shared Folders** → กด **Add…**
   - Host path: `C:\Users\Oping\CPE333_OS\Miniproj2`
   - Name: `Miniproj2`
   - เปิด **Enable this share** → OK
2. บูต VM เข้าเคอร์เนล `7.0.0-999-generic` เปิด Terminal (`Ctrl+Alt+T` ขยายตัวอักษรให้ชัดเจน) แล้วคัดลอกไฟล์เข้า ext4 ของ VM:

```bash
ls /mnt/hgfs/Miniproj2
rm -rf ~/miniproj2
cp -r /mnt/hgfs/Miniproj2 ~/miniproj2
cd ~/miniproj2
chmod +x scripts/*.sh
```

---

## Phase 2 — ตรวจสอบสภาพแวดล้อมและ Kernel Headers (`m01`–`m02`)

```bash
clear
uname -a
bash scripts/check_env.sh
```
📸 **`m01_env_check.png`**

```bash
clear
KDIR=/lib/modules/$(uname -r)/build
awk '/^(TRACE_EVENT|DEFINE_EVENT)\((sched_wakeup_template, )?(sched_switch|sched_wakeup|sched_migrate_task)/,/\);/' "$KDIR/include/trace/events/sched.h"
```
📸 **`m02_sched_h_proto.png`** *(หลักฐานยืนยัน signature ของ `sched_switch` 4 args และ `sched_migrate_task(p, dest_cpu)` ไม่มี `orig_cpu`)*

---

## Phase 3 — คอมไพล์และทดสอบโหลด Kernel Module (`m03`–`m05`)

```bash
clear
make clean && make all
ls -lh kernel/sched_profiler.ko workloads/
```
📸 **`m03_build.png`**

```bash
clear
sudo insmod kernel/sched_profiler.ko
lsmod | grep sched_profiler
sudo dmesg | grep sched_profiler | tail -n 5
cat /proc/sched_profiler
```
📸 **`m04_insmod.png`**

ทดสอบการรับ Input ผ่าน **Command-Line Module Parameter** (`pid=<PID>`):

```bash
clear
sudo rmmod sched_profiler
sudo insmod kernel/sched_profiler.ko pid=999999   # ต้องล้มเหลว: Invalid parameters
echo $$
sudo insmod kernel/sched_profiler.ko pid=$$
head -n 12 /proc/sched_profiler
echo 999999 > /proc/sched_profiler                # ต้องล้มเหลว: No such process
echo stop > /proc/sched_profiler
head -n 6 /proc/sched_profiler
```
📸 **`m05_module_param.png`**

---

## Phase 4 — ทดสอบ Workloads ทั้ง 3 รูปแบบ (`m06`–`m09`)

1. **Workload 1: CPU-Bound (Preemption & Time-Slice Context Switch)**

```bash
clear
./workloads/sched_profiler_ctl --run ./workloads/cpu_workload 4 --compete
```
📸 **`m06_cpu_workload.png`**

2. **Workload 2: I/O-Bound / Sleep (Woken Up & Voluntary Switch)**

```bash
clear
./workloads/sched_profiler_ctl --run ./workloads/sleep_workload 40 50
```
📸 **`m07_sleep_workload.png`** *(สังเกต `Woken Up` ~ 40–41 และ `Scheduled Out (voluntary)` ตรงกับจำนวนรอบที่หลับแล้วตื่น)*

3. **Workload 3A: CPU Migration (`sched_setaffinity`)**

```bash
clear
./workloads/sched_profiler_ctl --run ./workloads/migrate_workload 25 40 --mode affinity
```
📸 **`m08_migrate_workload.png`**

4. **Workload 3B: ทดสอบด้วย Command Line มาตรฐาน (`echo PID > /proc/...` + `taskset`)**

```bash
clear
./workloads/cpu_workload 0 >/dev/null &
TARGET_PID=$!
echo $TARGET_PID > /proc/sched_profiler
for cpu in 0 1 2 3 4 5 6 7 8 9 10 11; do
    taskset -cp $cpu $TARGET_PID >/dev/null
    sleep 0.1
done
kill $TARGET_PID
wait $TARGET_PID 2>/dev/null || true
cat /proc/sched_profiler
```
📸 **`m09_taskset_migrate.png`**

*(หรือรันทั้ง Demo 1–3B รวดเดียวพร้อมบันทึก log: `bash scripts/run_demo.sh --save`)*

---

## Phase 5 — Cross-Validation กับ `perf` และ `ftrace` (`m10`–`m11`)

> `/usr/bin/perf` ใช้กับเคอร์เนล `7.0.0-999-generic` ไม่ได้ เพราะไม่มีแพ็กเกจ `linux-tools` ของเคอร์เนลที่ build เอง ให้ติดตั้ง tools ของเคอร์เนลเดิมแทน
> `sudo apt install -y linux-tools-common linux-tools-7.0.0-31-generic`
> แล้ว `validate_perf.sh` จะเลือกใช้ `/usr/lib/linux-tools/7.0.0-31-generic/perf` ให้เอง (perf คุยกับเคอร์เนลผ่าน `perf_event_open()` ซึ่งเป็น ABI คงที่)

```bash
clear
sudo bash scripts/validate_perf.sh
```
📸 **`m10_validate_perf.png`**

```bash
clear
sudo bash scripts/validate_ftrace.sh
```
📸 **`m11_validate_ftrace.png`**

---

## Phase 6 — ถอด Module และตรวจสอบความปลอดภัย (`m12`)

```bash
clear
sudo rmmod sched_profiler
lsmod | grep -c sched_profiler
ls -l /proc/sched_profiler
sudo dmesg | sed -n '/sched_profiler: loading out-of-tree/,$p' | grep -E 'sched_profiler|BUG|Oops|WARNING|Call Trace' | tail -n 14
```
📸 **`m12_rmmod_dmesg.png`**

*`sed -n '/.../,$p'` ตัด dmesg ให้เริ่มตั้งแต่โหลดโมดูลครั้งแรก เพราะตอนบูต (วินาทีที่ 1.05) VMware มี `WARNING ... init_hybrid_pmu` ซึ่งเกิดก่อนโหลดโมดูลหลายนาทีและไม่เกี่ยวกับงานนี้ ควรระบุไว้ในรายงานด้วย ไม่ใช่ซ่อน*

*(คัดลอก log/screenshots กลับออกทาง Shared Folder ได้ด้วย `cp -r ~/miniproj2/result/* /mnt/hgfs/Miniproj2/result/`)*

---

## Phase 7 (ตามโจทย์ Re-build Kernel Files) — In-Tree Kernel Build (`m13`–`m16`)

รายละเอียดและเหตุผลของแต่ละขั้นอยู่ใน [`kernel/in_tree_integration.md`](../kernel/in_tree_integration.md) ใช้ kernel source และ build directory เดิมของ Mini-Project 1 จึงไม่ต้อง build ใหม่ทั้งหมด 106 นาที

```bash
cd ~/kernel/linux-*/
bash ~/miniproj2/kernel/intree_patch.sh .     # m13: ใส่ไฟล์ + Kconfig + Makefile + CONFIG_SCHED_PROFILER=m
bash ~/miniproj2/kernel/intree_build.sh .     # m14: debian/rules rebuild แบบ incremental
sudo modprobe sched_profiler; sudo dmesg | tail -n 5          # m15: BTF ไม่ตรง -> Invalid argument
# m16: ตัด .BTF, sign-file, install, depmod, modprobe (คำสั่งครบใน in_tree_integration.md ข้อ 4)
```
📸 **`m13_intree_patch.png`**, **`m14_intree_build.png`**, **`m15_intree_btf_mismatch.png`**, **`m16_intree_modprobe.png`**
