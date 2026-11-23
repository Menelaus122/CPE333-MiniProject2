# Plan Audit: ตรวจสอบความครอบคลุมและจุดแก้ไขของ Guide เทียบกับโจทย์ Mini-Project 2

เอกสารนี้สรุปผลการตรวจสอบเอกสารแนวทาง [`implementation_guide_process_scheduler_profiler (1).md`](../implementation_guide_process_scheduler_profiler%20(1).md) เทียบกับข้อกำหนดทั้งหมดใน [`mini_project_2_cpu_virtualization.md`](../mini_project_2_cpu_virtualization.md) และสภาพแวดล้อม VM จาก Mini-Project 1 (`CPE333-kernel`, Ubuntu 24.04.5 LTS, 12 vCPU, Kernel `7.0.0-999-generic`)

---

## 1. ตารางสรุปความครอบคลุมตามข้อกำหนด (Requirements Coverage Matrix)

| หัวข้อในโจทย์ (`mini_project_2_cpu_virtualization.md`) | สถานะใน Guide เดิม | การแก้ไข/เติมเต็มในโปรเจกต์นี้ | ไฟล์ที่รับผิดชอบ |
|---|---|---|---|
| **Re-build kernel files** เพื่อทำงานตามเป้าหมาย | ขาด (มีแค่แนวคิด LKM ไม่มีขั้นตอน in-tree) | เพิ่มคู่มือ integration เข้า Kernel Source (`drivers/misc/Kconfig`, `Makefile`) และ rebuild `.deb` | [`kernel/in_tree_integration.md`](../kernel/in_tree_integration.md) |
| **รับค่า Input PID** ผ่าน `/proc` หรือ Command Line Arguments | ครอบคลุมบางส่วน (มีแค่ `/proc` write) | รองรับครบทั้ง 3 ช่องทาง: Module Parameter (`insmod ... pid=`), `/proc/sched_profiler` (`<PID>`, `pid`, `tgid`, `reset`, `stop`), และ CLI wrapper | [`kernel/sched_profiler.c`](../kernel/sched_profiler.c), [`workloads/sched_profiler_ctl.c`](../workloads/sched_profiler_ctl.c) |
| **ติดตาม Scheduler & นับ 4 Metrics** (`sched_in`, `sched_out`, `woken_up`, `migrate`) | มีแนวคิด แต่ **มีบั๊ก Signature ของ Tracepoint** | แก้ Signature ให้ตรง Kernel 5.19–7.0+, แยกนับ Involuntary vs Voluntary (เงื่อนไขเดียวกับ `nivcsw`/`nvcsw`), รองรับทั้ง `PID` และ `TGID` | [`kernel/sched_profiler.c`](../kernel/sched_profiler.c) |
| **Tracepoints 3 ตัว** (`sched_switch`, `sched_wakeup`, `sched_migrate_task`) | มีชื่อครบ แต่คำอธิบายพารามิเตอร์ผิดจุด | ตรวจ Compile-time ด้วย `check_trace_callback_type_*()` และดึง `orig_cpu = task_cpu(p)` ถูกต้อง | [`kernel/sched_profiler.c`](../kernel/sched_profiler.c) |
| **Concurrency & Safety** | แนะนำแค่ `atomic64_t` | ใช้ `atomic64_t` + `ctrl_mutex` + `synchronize_rcu()` + `smp_wmb()` กัน Reset Race และ `tracepoint_synchronize_unregister()` | [`kernel/sched_profiler.c`](../kernel/sched_profiler.c) |
| **การเปรียบเทียบกับ `ftrace` และ `perf`** | **คำสั่ง `perf stat -p` ผิด** ไม่มีสคริปต์จริง | สร้างสคริปต์ Cross-Validation เทียบผลกับ `perf record -a` และ `tracefs (ftrace)` พร้อมตาราง Diff | [`scripts/validate_perf.sh`](../scripts/validate_perf.sh), [`scripts/validate_ftrace.sh`](../scripts/validate_ftrace.sh) |
| **Demo การทำงานจริง (Workload 3 รูปแบบ)** | มีแค่คำอธิบายข้อความ | สร้างโปรแกรม C ทดสอบทั้ง 3 รูปแบบ พร้อม `--compete` แก้ปัญหา VM 12 vCPU และสคริปต์ Demo | [`workloads/`](../workloads/), [`scripts/run_demo.sh`](../scripts/run_demo.sh) |
| **ทฤษฎี `kernel/tracepoint.c` & `docs.kernel.org/trace/tracepoints.html`** | **ขาดทั้งหมด** | สรุปกลไก `static_key`/jump label, `__tracepoints_ptrs`, RCU callback array ครบถ้วน | [`docs/THEORY_AND_REPORT_GUIDE.md`](THEORY_AND_REPORT_GUIDE.md) |
| **ทฤษฎี `DECLARE_TRACE` vs `TRACE_EVENT`** | สั้นเกินไป (ขาด `DECLARE_EVENT_CLASS` / `DEFINE_EVENT` / `CREATE_TRACE_POINTS`) | อธิบายการขยาย Macro พร้อมตัวอย่างจริงจาก `sched.h` และตารางเปรียบเทียบ | [`docs/THEORY_AND_REPORT_GUIDE.md`](THEORY_AND_REPORT_GUIDE.md) |
| **รายงานไม่เกิน 25 หน้า (ไม่รวมภาคผนวก)** | **ขาดทั้งหมด** | วางโครงสร้างและ Page Budget (22–24 หน้าเนื้อหา + ภาคผนวก A–D) + Runbook เก็บภาพ `m01–m14` | [`docs/THEORY_AND_REPORT_GUIDE.md`](THEORY_AND_REPORT_GUIDE.md), [`docs/COMMANDS.md`](COMMANDS.md) |
| **Ultra-presentation 7–11 นาที (กลุ่ม 6–8 คน)** | **ขาดทั้งหมด** | สคริปต์และโครงสไลด์ 12 หน้า (เป้า 9:30 นาที) แบ่งหน้าที่ 6–8 คน + แผน Live/Backup Demo | [`docs/PRESENTATION_PLAN.md`](PRESENTATION_PLAN.md) |

---

## 2. บั๊กและกับดักทางเทคนิคที่ตรวจพบใน Guide เดิม (5 ข้อสำคัญ)

1. **CRITICAL BUG — Signature ของ `sched_migrate_task` ใน Guide §2.3:**
   - **ปัญหาใน Guide:** เขียนให้เข้าใจว่าฟังก์ชัน Probe ได้รับทั้ง `orig_cpu` และ `dest_cpu` เป็นพารามิเตอร์ (`orig_cpu != dest_cpu`)
   - **ความจริงใน Linux Kernel (`include/trace/events/sched.h`):** Prototype คือ `TP_PROTO(struct task_struct *p, int dest_cpu)` (ไม่มี `orig_cpu` ส่งเข้ามา!) มีเพียง `void *data`, `struct task_struct *p`, `int dest_cpu` หากประกาศฟังก์ชันผิดจำนวนพารามิเตอร์จะอ่านค่าขยะจาก Register หรือทำให้ Kernel Panic ได้
   - **วิธีแก้:** อ่าน `orig_cpu = task_cpu(p)` ภายใน Callback (ซึ่งถูกเรียกก่อน `__set_task_cpu()` จึงได้ CPU ต้นทางที่ถูกต้อง) และตรวจสอบระดับ Compile-time ด้วย `check_trace_callback_type_sched_migrate_task()`

2. **CRITICAL BUG — คำสั่ง `perf stat -p $TARGET_PID` ใน Guide §5 (บรรทัด 130):**
   - **ปัญหาใน Guide:** `sudo perf stat -e sched:sched_switch,sched:sched_wakeup,sched:sched_migrate_task -p $TARGET_PID sleep 5`
   - **ความจริงใน Linux `perf`:** Flag `-p $PID` จะผูกกับ Context ของโปรเซสเมื่อโปรเซสนั้นเป็น `current` บน CPU เท่านั้น ทำให้:
     - `sched_wakeup` เกิดขึ้นใน Context ของ **ผู้ปลุก (waker)** เช่น `swapper` หรือ timer interrupt ซึ่งขณะนั้น `current != target` -> `perf stat -p` มักนับได้ `0`
     - `sched_migrate_task` มักถูกเรียกจาก Load Balancer / Kworker / CPU อื่นขณะโปรเซสเป้าหมายไม่ได้กำลังรันอยู่ (`current != target`) -> `perf stat -p` พลาดเหตุการณ์ส่วนใหญ่
     - `sched_switch` นับรวมแค่ขาที่สัมพันธ์กับ `current` ไม่แยก `Scheduled In` ออกจาก `Scheduled Out`
   - **วิธีแก้:** ใช้ `perf record -a` (system-wide) คู่กับ `--filter` บน Payload ของ Event (`prev_pid == $PID || next_pid == $PID` และ `pid == $PID`) แล้วแยกนับด้วย `perf script | awk` (ดู [`scripts/validate_perf.sh`](../scripts/validate_perf.sh)) และจำกัด Filter ใน `ftrace` ให้เทียบเฉพาะ Field กับค่าคงที่เท่านั้น (เช่น `pid == $PID`) แล้วตรวจ `orig_cpu != dest_cpu` ใน `awk` (ดู [`scripts/validate_ftrace.sh`](../scripts/validate_ftrace.sh))

3. **API Changes บน Linux 5.19 – 7.0 (`CPE333-kernel` รัน Kernel `7.0.0-999-generic`):**
   - `sched_switch` มีพารามิเตอร์ 4 ตัวตั้งแต่ Kernel 5.19: `(bool preempt, struct task_struct *prev, struct task_struct *next, unsigned int prev_state)` โมดูลต้องใช้ `prev_state` ในการแยก voluntary/involuntary จึงกำหนดให้ต้องเป็น Kernel 5.19 ขึ้นไป (`#error` ถ้าต่ำกว่า)
   - `get_task_comm()` ใน Kernel รุ่นใหม่รับเฉพาะ array ขนาดคงที่ (มี `BUILD_BUG_ON(sizeof(buf) < TASK_COMM_LEN)`) ส่ง pointer เข้าไปไม่ได้ โค้ดจึงใช้ `strscpy(buf, task->comm, len)` ภายใต้ `rcu_read_lock()` แทน
   - Path ของ Tracefs บนดิสโทรยุคใหม่คือ `/sys/kernel/tracing` (รองรับ `/sys/kernel/debug/tracing` เป็น fallback)
   - กำหนด Permission ของ `/proc/sched_profiler` เป็น `0666` เพื่อให้ Demo ด้วย `echo $PID > /proc/sched_profiler` โดยไม่ต้องสลับเป็น root ทำได้ราบรื่น

4. **Reset Race Condition เมื่อสลับ PID หรือสั่ง `reset`:**
   - **ปัญหา:** หากเขียน `target_pid = new_pid` แล้วล้าง Counter เป็น `0` ทันที อาจมี Tracepoint Probe ที่กำลังทำงานอยู่บน Core อื่นบวกค่าเก่าทับหลังรีเซ็ต
   - **วิธีแก้:** ล็อก `ctrl_mutex`, ตั้ง `target_pid = -1`, เรียก `synchronize_rcu()` เพื่อรอให้ Callback บนทุก CPU จบก่อน, ล้างตัวนับเป็น `0`, ใส่ `smp_wmb()`, แล้วจึงเปิด `target_pid = new_pid`

5. **12-vCPU Demo Flaw ใน Guide §5 Workload 1:**
   - **ปัญหา:** บน VM 12 vCPU ที่เครื่องว่าง การรัน `while(1)` เพียงโปรเซสเดียวจะไม่ถูกแย่ง CPU บ่อย (`sched_out` ต่ำมาก)
   - **วิธีแก้:** [`workloads/cpu_workload.c`](../workloads/cpu_workload.c) รองรับ `--compete` เพื่อ fork โปรเซสคู่แข่งและ Pin ทั้งคู่ลงบน CPU Core เดียวกัน บังคับให้ Kernel Preempt และสลับ Context Switch ต่อเนื่องชัดเจนทุกครั้งที่ Demo
