# Implementation Guide: Process Scheduler Profiler (CPU Virtualization)

เอกสารนี้เป็นแนวทางการออกแบบและพัฒนา **Process Scheduler Profiler** ในรูปแบบของ Linux Kernel Module (LKM) หรือการแก้ไข Kernel เพื่อติดตามพฤติกรรมการจัดตารางเวลา (Scheduling) ของโปรเซสเป้าหมายตาม PID ที่กำหนด

---

## 1. ภาพรวมสถาปัตยกรรมระบบ (System Architecture)

ระบบจะทำงานในระดับ **Kernel Space** โดยอาศัยกลไกของ **Linux Kernel Tracepoints** เพื่อดักจับเหตุการณ์ที่เกิดขึ้นกับโปรเซสแบบ Real-time และเปิดช่องทางให้ **User Space** สื่อสารผ่านระบบไฟล์เสมือน **`/proc`**

```text
+-------------------------------------------------------------------+
|                           User Space                              |
|                                                                   |
|   [Command: echo <PID> > /proc/sched_profiler]                    |
|   [Command: cat /proc/sched_profiler]                             |
+---------------------------------+---------------------------------+
                                  |  Read / Write
                                  v
+---------------------------------+---------------------------------+
|                          Kernel Space                             |
|                                                                   |
|   +-----------------------------------------------------------+   |
|   |               /proc/sched_profiler Interface              |   |
|   |  - proc_write(): รับค่า Target PID และ Reset ตัวนับ          |   |
|   |  - proc_read() / seq_file: แสดงผลสถิติการทำงาน              |   |
|   +-----------------------------+-----------------------------+   |
|                                 |                                 |
|   +-----------------------------v-----------------------------+   |
|   |                 Profiler Core & Counters                  |   |
|   |  - target_pid (pid_t)                                     |   |
|   |  - sched_in_count, sched_out_count (atomic64_t)           |   |
|   |  - wakeup_count, migrate_count (atomic64_t)               |   |
|   +-----------------------------^-----------------------------+   |
|                                 |  Callback Functions             |
|   +-----------------------------+-----------------------------+   |
|   |               Linux Kernel Tracepoints                    |   |
|   |  1. sched_switch       -> นับ Sched In / Sched Out        |   |
|   |  2. sched_wakeup       -> นับ Woken Up                    |   |
|   |  3. sched_migrate_task -> นับ CPU Core Migration          |   |
|   +-----------------------------------------------------------+   |
+-------------------------------------------------------------------+
```

---

## 2. การทำงานของ Kernel Tracepoints ที่เกี่ยวข้อง

ในการดักจับเหตุการณ์ของ Scheduler เราจะลงทะเบียน (Register) ฟังก์ชัน Probe เข้ากับ Tracepoints หลัก 3 ตัวของ Linux Kernel (อ้างอิงจาก `include/trace/events/sched.h`):

### 2.1 `sched_switch` (ติดตามการ Scheduled In และ Scheduled Out)
* **เมื่อใดที่ถูกเรียก:** เกิดขึ้นทุกครั้งที่ CPU ทำการ Context Switch จากโปรเซสหนึ่ง (`prev`) ไปยังอีกโปรเซสหนึ่ง (`next`)
* **ตรรกะการนับ:**
  * **Scheduled Out:** ตรวจสอบว่า `prev->pid == target_pid` หากตรงกัน ให้เพิ่มค่า `sched_out_count` (โปรเซสเป้าหมายกำลังถูกสลับออกจาก CPU)
  * **Scheduled In:** ตรวจสอบว่า `next->pid == target_pid` หากตรงกัน ให้เพิ่มค่า `sched_in_count` (โปรเซสเป้าหมายกำลังได้รับสิทธิ์เข้าทำงานบน CPU)

### 2.2 `sched_wakeup` (ติดตามการถูกปลุกให้ตื่น)
* **เมื่อใดที่ถูกเรียก:** เกิดขึ้นเมื่อโปรเซสที่อยู่ในสถานะหลับ (Sleeping/Waiting เช่น รอ I/O หรือครบกำหนด `sleep()`) ถูกปลุกให้เปลี่ยนสถานะเป็น `TASK_RUNNING` และพร้อมเข้าคิวรอทำงาน
* **ตรรกะการนับ:**
  * ตรวจสอบว่า `p->pid == target_pid` (เมื่อ `p` คือ `struct task_struct *` ของโปรเซสที่ถูกปลุก) หากตรงกัน ให้เพิ่มค่า `wakeup_count`

### 2.3 `sched_migrate_task` (ติดตามการย้ายข้าม CPU Core)
* **เมื่อใดที่ถูกเรียก:** เกิดขึ้นเมื่อ Load Balancer ของ Scheduler ตัดสินใจย้ายโปรเซสจาก Runqueue ของ CPU หนึ่ง (`orig_cpu`) ไปยังอีก CPU หนึ่ง (`dest_cpu`)
* **ตรรกะการนับ:**
  * ตรวจสอบว่า `p->pid == target_pid` และ `orig_cpu != dest_cpu` หากตรงกัน ให้เพิ่มค่า `migrate_count`

> **หมายเหตุสำคัญ (Concurrency & Synchronization):**
> เนื่องจาก Tracepoints สามารถถูกเรียกใช้งานพร้อมกันจากหลาย CPU Cores ได้ตลอดเวลา ตัวแปรนับจำนวนทั้งหมดควรประกาศเป็นชนิด `atomic64_t` (เช่น `atomic64_inc(&sched_in_count)`) เพื่อป้องกันปัญหา **Race Condition**

---

## 3. แนวคิดเชิงทฤษฎีที่ควรระบุในรายงาน (Hints)

### 3.1 `DECLARE_TRACE` vs `TRACE_EVENT`
* **`DECLARE_TRACE`:** เป็น Macro พื้นฐานสำหรับประกาศจุด Tracepoint เปล่าๆ โดยไม่ได้สร้างโครงสร้างการบันทึกข้อมูลลงใน Ring Buffer (ftrace) โดยอัตโนมัติ เหมาะสำหรับการนำไปผูกกับฟังก์ชัน Probe ที่เขียนขึ้นเอง (เช่นใน Kernel Module ของเรา)
* **`TRACE_EVENT`:** เป็น Macro ที่สร้างบน `DECLARE_TRACE` อีกชั้นหนึ่ง โดยจะสร้างโครงสร้างครบวงจรทั้ง Tracepoint, ฟังก์ชันจัดเก็บข้อมูลลง Ring Buffer, และรูปแบบการแสดงผลข้อความ (Print format) เพื่อให้เครื่องมืออย่าง `ftrace` และ `perf` นำไปใช้งานได้ทันที

### 3.2 การเปรียบเทียบกับเครื่องมือมาตรฐาน (`ftrace` / `perf`)
* ในขั้นตอนการพัฒนา สามารถใช้คำสั่ง `perf sched record` หรือเปิดใช้งาน `/sys/kernel/debug/tracing/events/sched/` เพื่อดูผลลัพธ์มาตรฐาน และนำมาเปรียบเทียบความถูกต้อง (Validate) กับตัวเลขที่ได้จาก Profiler ของกลุ่ม

---

## 4. ขั้นตอนการพัฒนา (Step-by-Step Implementation)

### ขั้นตอนที่ 1: เตรียมสภาพแวดล้อมและ Kernel Source
1. ตรวจสอบเวอร์ชันของ Linux Kernel ที่ใช้งาน (`uname -r`) และดาวน์โหลด Kernel Source Code ให้ตรงเวอร์ชัน
2. ตรวจสอบให้แน่ใจว่า Kernel เปิดใช้งาน Config `CONFIG_TRACEPOINTS=y` และ `CONFIG_MODULES=y`
3. ศึกษา Signature ของฟังก์ชัน Tracepoint ในไฟล์ `include/trace/events/sched.h` ของ Kernel เวอร์ชันนั้นๆ (เนื่องจากพารามิเตอร์ของฟังก์ชัน Probe อาจแตกต่างกันเล็กน้อยในแต่ละเวอร์ชัน)

### ขั้นตอนที่ 2: การค้นหาและลงทะเบียน Tracepoints
ในบางเวอร์ชันของ Kernel สัญลักษณ์ `__tracepoint_sched_switch` อาจไม่ได้ถูก Export ด้วย `EXPORT_SYMBOL_GPL` โดยตรง ทำให้ Kernel Module เรียกใช้ตรงๆ ไม่ได้ วิธีแก้ปัญหาคือ:
1. ใช้ฟังก์ชัน `for_each_kernel_tracepoint()` เพื่อวนลูปค้นหา `struct tracepoint *` จากชื่อ `"sched_switch"`, `"sched_wakeup"`, และ `"sched_migrate_task"`
2. เมื่อพบ Pointer ของแต่ละ Tracepoint แล้ว ให้ใช้ `tracepoint_probe_register()` เพื่อผูกฟังก์ชัน Callback ของเราเข้าไปในตอนโหลดโมดูล (`module_init`)
3. คืนค่าและยกเลิกการลงทะเบียนด้วย `tracepoint_probe_unregister()` และ `tracepoint_synchronize_unregister()` ในตอนถอดโมดูล (`module_exit`) เพื่อป้องกัน Kernel Panic

### ขั้นตอนที่ 3: สร้าง `/proc` File Interface
1. สร้างไฟล์ `/proc/sched_profiler` ด้วย `proc_create()` โดยใช้ `struct proc_ops` (สำหรับ Kernel 5.6 ขึ้นไป)
2. **ฟังก์ชัน `write`:** รับค่า String จาก User Space แปลงเป็นตัวเลข PID ด้วย `kstrtoint()` บันทึกลงตัวแปร `target_pid` และสั่ง Reset ตัวนับทั้งหมดเป็น `0`
3. **ฟังก์ชัน `read`:** ใช้ `seq_file` API (`single_open`, `seq_printf`) เพื่อจัดรูปแบบข้อความและแสดงค่าสถิติทั้งหมดกลับไปยัง User Space

---

## 5. แผนการทดสอบและสาธิต (Testing & Demo Plan)

เพื่อให้เห็นการทำงานของทั้ง 4 ตัวชี้วัดอย่างชัดเจนในวัน Demo ควรสร้างโปรแกรมทดสอบ (Workload) ใน User Space 3 รูปแบบ:

1. **CPU-Bound Workload (ทดสอบการ Preemption / Context Switch):**
   * รันลูปคำนวณหนักๆ (`while(1)`) บนระบบที่มีโปรเซสอื่นทำงานร่วมด้วย เพื่อดูค่า **Scheduled In / Out** ที่เพิ่มขึ้นอย่างต่อเนื่องจากการถูกตัดจังหวะ (Time slice หมด)
2. **I/O-Bound / Sleep Workload (ทดสอบการ Woken Up):**
   * รันโปรแกรมที่เรียก `sleep()` หรือ `usleep()` สลับกับการพิมพ์ข้อความ เพื่อยืนยันว่าค่า **Woken Up** จะเพิ่มขึ้นทุกครั้งที่โปรเซสตื่นจากการหลับ
3. **Multi-Core Migration Workload (ทดสอบการย้าย Core):**
   * รันโปรเซสหลายตัวพร้อมกันให้เต็มจำนวน CPU Cores หรือใช้คำสั่ง `taskset` บังคับย้าย Core ของโปรเซสเป้าหมายไปมา เพื่อตรวจสอบว่าตัวนับ **CPU Core Migration** ทำงานได้ถูกต้องแม่นยำ

### ตัวอย่างคำสั่งระหว่างการ Demo:
```bash
# 1. โหลด Kernel Module เข้าสู่ระบบ
sudo insmod sched_profiler.ko

# 2. รันโปรแกรมทดสอบใน Background และเก็บค่า PID
./test_workload &
TARGET_PID=$!

# 3. ส่งค่า PID ให้ Profiler เริ่มติดตาม
echo $TARGET_PID > /proc/sched_profiler

# 4. ดูผลลัพธ์การทำงาน
cat /proc/sched_profiler

# 5. เปรียบเทียบความถูกต้องด้วย perf (Optional)
sudo perf stat -e sched:sched_switch,sched:sched_wakeup,sched:sched_migrate_task -p $TARGET_PID sleep 5
```