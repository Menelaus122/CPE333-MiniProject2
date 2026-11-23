# Theory & Report Blueprint: Process Scheduler Profiler (CPE333 Mini-Project 2)

เอกสารนี้รวบรวมเนื้อหาเชิงทฤษฎีที่ต้องมีตามโจทย์ [`mini_project_2_cpu_virtualization.md`](../mini_project_2_cpu_virtualization.md) และจัดโครงสร้าง **รายงานฉบับสมบูรณ์ไม่เกิน 25 หน้า (ไม่รวมภาคผนวก)**

---

## 1. ทฤษฎี CPU Virtualization และ Linux Process Scheduler

### 1.1 แนวคิด CPU Virtualization
ระบบปฏิบัติการสร้างภาพลวงตาให้แต่ละโปรเซสเสมือนว่าครอบครอง CPU อยู่เพียงลำพังผ่านเทคนิค **Time-Sharing (Limited Direct Execution)**:
- **Context Switch:** Kernel หยุดโปรเซสปัจจุบัน (`prev`), บันทึก Register/Program Counter/Kernel Stack ลงใน `task_struct` / `thread_info`, เปลี่ยน Page Table (`mm_struct`) หากเป็นคนละ Process, แล้วโหลดสถานะของโปรเซสถัดไป (`next`)
- **Voluntary vs Involuntary (Preemption) Switch:**
  - *Voluntary (`prev_state != TASK_RUNNING` และ `preempt == false`):* โปรเซสสละ CPU เองเพราะกำลังจะหลับ เช่น รอ I/O, เรียก `sleep()` / `nanosleep()`, หรือรอ Mutex (`TASK_INTERRUPTIBLE` / `TASK_UNINTERRUPTIBLE`) Kernel นับใน `nvcsw`
  - *Involuntary (`preempt == true` หรือ `prev_state == TASK_RUNNING`):* หมดเวลา Time Slice หรือมีงานที่ Deadline เร่งด่วนกว่าแย่ง CPU (`TIF_NEED_RESCHED`) Kernel นับใน `nivcsw`
  - **ข้อควรระวัง:** ดูแค่ `preempt` ไม่พอ โปรเซส CPU-bound ใน user space ที่หมด slice จะถูกสลับตอนกลับสู่ user space ผ่าน `schedule()` ซึ่งส่ง `preempt == false` แต่ `prev_state` ยังเป็น `TASK_RUNNING` โมดูลจึงใช้เงื่อนไขเดียวกับ `__schedule()` และผลตรงกับ `getrusage()` (ภาพ `m06`: involuntary 683 = `nivcsw` 683)

### 1.2 Linux Scheduler: จาก CFS สู่ EEVDF (Linux 6.6 – 7.0)
- **CFS (Completely Fair Scheduler, 2.6.23 – 6.5):** ใช้โครงสร้าง Red-Black Tree เรียงตาม `vruntime` เพื่อแบ่งเวลา CPU อย่างยุติธรรม
- **EEVDF (Earliest Eligible Virtual Deadline First, Linux 6.6+ รวมถึง 7.0 ใน VM):** พัฒนาต่อจาก CFS โดยเพิ่มแนวคิด **Eligible Time** และ **Virtual Deadline ($v_d = v_e + \text{slice}/w_i$)** ช่วยให้โปรเซสที่มี slice สั้น (เช่น งาน Interactive / Latency-sensitive) ตอบสนองได้เร็วขึ้นโดยยังคงความเป็นธรรม
- **Wakeup Path (`try_to_wake_up` → `ttwu_do_activate`):** เมื่อทรัพยากรพร้อมหรือครบกำหนด sleep เคอร์เนลเปลี่ยนสถานะจาก Sleeping เป็น `TASK_RUNNING`, นำกลับเข้า Runqueue (`rq`), เรียก **`sched_wakeup`**, และอาจตั้ง `resched_curr()` หากควร preempt โปรเซสปัจจุบัน
- **SMP Load Balancing & Migration (`set_task_cpu`):** บนระบบ Multi-core (เช่น VM 12 vCPU) แต่ละ Core มี Runqueue แยกของตัวเอง (`struct rq`) การย้ายโปรเซสข้าม Core เกิดจาก 3 สาเหตุหลัก:
  1. **Periodic / Idle Load Balancing:** Core ที่ว่างดึงงานจาก Core ที่มีคิวยาว
  2. **Wakeup Affinity (`select_task_rq`):** ปลุกงานไปวางบน Core ที่แคชอุ่นหรือ Core ที่ว่างกว่า
  3. **Explicit CPU Affinity (`sched_setaffinity` / `taskset`):** ผู้ใช้หรือระบบบังคับย้าย Core

---

## 2. กลไกภายใน Linux Kernel Tracepoints (`kernel/tracepoint.c`)

อ้างอิงจาก [Linux Kernel `kernel/tracepoint.c`](https://github.com/torvalds/linux/blob/master/kernel/tracepoint.c) และ [Linux Kernel Tracepoints Documentation](https://docs.kernel.org/trace/tracepoints.html):

### 2.1 โครงสร้างและการออกแบบแบบ Zero/Near-Zero Overhead
Tracepoint คือจุด Hook แบบ Static ที่ฝังไว้ตามจุดสำคัญในโค้ด Kernel (เช่น `context_switch()`, `try_to_wake_up()`, `set_task_cpu()`):
- **Static Key / Jump Label Patching (`__DO_TRACE`):** ในสภาวะปกติที่ยังไม่มี Probe มาเกาะ คอมไพเลอร์และ Kernel ใช้ `static_key_false(&tp->key)` ซึ่งแปลงเป็นคำสั่ง **`NOP` เพียง 1 คำสั่ง** ในตำแหน่ง Hot Path ของ Kernel (แทบไม่เสีย CPU cycles และไม่แตก branch)
- เมื่อมีการลงทะเบียน Probe ตัวแรก `kernel/tracepoint.c` จะ **Patch Binary Code ณ Runtime** เปลี่ยน `NOP` ให้เป็นคำสั่ง Jump โดยตรงไปยัง Loop ที่เรียกฟังก์ชัน Probe (`tp->funcs`)

### 2.2 โครงสร้างข้อมูลหลักใน `kernel/tracepoint.c`
- **`struct tracepoint`:** เก็บ `name`, `static_key key`, `struct tracepoint_func __rcu *funcs` (array ของ callback + private data + priority) และ callback สำหรับจัดการ register/unregister (`regfunc`, `unregfunc`)
- **`__tracepoints_ptrs` Linker Section:** ตัวชี้ไปยังทุก `struct tracepoint` ใน Core Kernel ถูกรวบรวมไว้ระหว่าง `__start___tracepoints_ptrs` ถึง `__stop___tracepoints_ptrs`
- **ฟังก์ชันสำคัญสำหรับ Kernel Module:**
  1. `for_each_kernel_tracepoint(fct, priv)` — วนอ่านทุก Tracepoint ในตาราง `__tracepoints_ptrs` (จำเป็นเมื่อสัญลักษณ์ `__tracepoint_*` ไม่ได้ถูก Export หรือต้องการค้นหาด้วยชื่อแบบ Dynamic)
  2. `tracepoint_probe_register(tp, probe, data)` — ล็อก `tracepoints_mutex`, คัดลอก Array `tp->funcs` ใหม่แบบ Copy-on-Write (RCU), สลับ Pointer ด้วย `rcu_assign_pointer()`, และเปิด `static_key` หากเป็น Probe แรก
  3. `tracepoint_probe_unregister(tp, probe, data)` — ถอด Callback ออกจาก Array ใหม่ และปิด `static_key` หากไม่เหลือ Probe
  4. `tracepoint_synchronize_unregister()` — เรียก `synchronize_rcu()` เพื่อรอให้ทุก CPU ที่กำลังรัน Callback เดิมภายใต้ `rcu_read_lock_sched` / Preempt-disable ทำงานจนจบก่อนที่ Module จะถูกปลด (`rmmod`) จากหน่วยความจำ **หากลืมเรียกขั้นตอนนี้จะเกิด Use-After-Free / Kernel Panic**

---

## 3. เปรียบเทียบ `DECLARE_TRACE` vs `TRACE_EVENT` vs `DECLARE_EVENT_CLASS`

| คุณสมบัติ | `DECLARE_TRACE` | `TRACE_EVENT` | `DECLARE_EVENT_CLASS` + `DEFINE_EVENT` |
|---|---|---|---|
| **ระดับชั้น (Abstraction)** | Macro พื้นฐานระดับล่าง (`include/linux/tracepoint.h`) | Macro ระดับสูงสร้างบน `DECLARE_TRACE` (`include/linux/trace_events.h`) | Macro แยก Template (`CLASS`) ออกจากการสร้าง Event ย่อย (`DEFINE_EVENT`) |
| **สิ่งที่สร้างให้อัตโนมัติ** | เฉพาะ `trace_<name>()` และฟังก์ชัน Register Probe | สร้างทั้ง Tracepoint + โครงสร้าง Ring Buffer (`TP_STRUCT__entry`), `TP_fast_assign`, `TP_printk` | สร้าง 1 Print/Assign Template ใช้ร่วมกันได้หลาย Event (ลดขนาด Kernel Binary) |
| **การรองรับ `ftrace` / `perf`** | **ไม่รองรับอัตโนมัติ** (ไม่มี Ring Buffer schema) | **รองรับทันที** ใน `/sys/kernel/tracing/events/` และ `perf` | **รองรับทันที** ทุก Event ที่ `DEFINE_EVENT` จาก Template |
| **การใช้งานจริงใน `sched.h`** | ใช้ภายใน Subsystem ที่ต้องการ Hook เฉพาะทาง | **`sched_switch`**, **`sched_migrate_task`** | **`sched_wakeup_template`** (Class) → **`sched_wakeup`**, `sched_waking`, `sched_wakeup_new` |

### บทบาทของ `#define CREATE_TRACE_POINTS`
- ปกติเมื่อ Include `<trace/events/sched.h>` โดย**ไม่**นิยาม `CREATE_TRACE_POINTS` Macro จะขยายเฉพาะ Declaration (`extern struct tracepoint ...` และ `check_trace_callback_type_*()`)
- เฉพาะไฟล์ต้นทางของ Kernel ที่เป็นเจ้าของ Tracepoint (เช่น `kernel/sched/core.c`) เท่านั้นที่ประกาศ `#define CREATE_TRACE_POINTS` เพื่อให้ Macro ขยายเป็น **Definition จริง** เพียงจุดเดียว (ป้องกัน Multiple Definition Link Error) ดังนั้นใน `sched_profiler.c` **ต้องไม่นิยาม `CREATE_TRACE_POINTS`**

---

## 4. สถาปัตยกรรม `ftrace` และ `perf` กับการทวนสอบผล (Validation)

- **`ftrace` (Function & Event Tracer):** ใช้ Ring Buffer ภายใน Kernel (`tracefs` ที่ `/sys/kernel/tracing`) มี Low-overhead filter engine
- **`perf` (`perf_events`):** ใช้ `perf_event_open()` syscall สร้าง Ring Buffer ต่อ CPU แล้ว mmap มายัง User Space
- **ข้อควรระวังสำคัญในการ Validate (เหตุผลที่ `perf stat -p $PID` ใน Guide เดิมพลาด):**
  - `-p $PID` ใน `perf` กรองเฉพาะ Event ที่เกิดขณะโปรเซสเป้าหมายกำลังถือ CPU (`current->pid == PID`)
  - **`sched_wakeup`** เกิดขึ้นใน Context ของโปรเซสผู้ปลุก (`swapper`, `kworker`, timer IRQ) และ **`sched_migrate_task`** มักเกิดจาก Load Balancer บนอีก Core หนึ่ง ขณะโปรเซสเป้าหมายไม่ได้เป็น `current`
  - ดังนั้นการ Validation ที่ถูกต้องต้องบันทึกแบบ **System-Wide (`-a`)** ร่วมกับ **Tracepoint Payload Filter** (`prev_pid == $PID || next_pid == $PID` สำหรับ `sched_switch` และ `pid == $PID` สำหรับ `sched_wakeup` / `sched_migrate_task`) ซึ่งสคริปต์ [`scripts/validate_perf.sh`](../scripts/validate_perf.sh) และ [`scripts/validate_ftrace.sh`](../scripts/validate_ftrace.sh) ทำให้โดยอัตโนมัติ

---

## 5. การออกแบบ Profiler และ การปรับแก้สำหรับ Linux 7.0

1. **Dual Input Interface:** รองรับทั้ง Module Parameter (`insmod sched_profiler.ko pid=<PID>`) และการเขียน/อ่านผ่าน `/proc/sched_profiler` รวมถึง CLI wrapper `sched_profiler_ctl`
2. **Thread vs Process (`PID` vs `TGID`):** รองรับทั้ง `p->pid` (ค่าเริ่มต้น ตรงกับ `sched_switch` / `sched_wakeup` ใน Kernel) และโหมด `tgid <PID>` สำหรับโปรเซส Multi-threaded
3. **Synchronization 2 ระดับ:**
   - **Data Path (Hot Path ใน Tracepoint):** Lock-free ด้วย `atomic64_t` (เพิ่ม Breakup ของ `involuntary` vs `voluntary` บน `sched_out`)
   - **Control Path (`proc_write` / Reset):** ใช้ `ctrl_mutex` + ปิด `target_pid = -1` + `synchronize_rcu()` + ล้างตัวนับ + `smp_wmb()` ก่อนเปิดค่า `target_pid` ใหม่
4. **ความเข้ากันได้กับ Linux 5.19–7.0 (`CPE333-kernel`):**
   - `sched_switch` 4 arguments (`unsigned int prev_state`) + `check_trace_callback_type_*()`
   - อ่านชื่อโปรเซสด้วย `strscpy(buf, task->comm, len)` ภายใต้ `rcu_read_lock()` เพราะ `get_task_comm()` ใน Kernel รุ่นใหม่รับได้เฉพาะ array ขนาดคงที่ (ตรวจ `sizeof(buf)`) ส่ง pointer ไม่ได้
   - `sched_switch`, `sched_wakeup`, `sched_migrate_task` ไม่ได้ถูก export ให้ module (ไม่มีใน `Module.symvers` ภาพ `m02`) จึงต้องหา `struct tracepoint *` ด้วย `for_each_kernel_tracepoint()` แล้วลงทะเบียนด้วย `tracepoint_probe_register()` ซึ่งทั้งสองตัวเป็น `EXPORT_SYMBOL_GPL`
   - `sched_migrate_task` อ่าน CPU ต้นทางผ่าน `task_cpu(p)` (ไม่รับ `orig_cpu` จากพารามิเตอร์)

---

## 6. โครงสร้างและ Page Budget ของรายงาน (เป้าหมาย 22–24 หน้า <= เกณฑ์ 25 หน้า)

> **กติกา:** ความยาวเนื้อหาหลักไม่เกิน **25 หน้า** (ไม่รวมปก สารบัญ และภาคผนวก)

| บทที่ | หัวข้อ | เป้าหมายจำนวนหน้า | แหล่งหลักฐาน / ภาพประกอบ |
|---|---|---:|---|
| *ส่วนหน้า* | หน้าปก, คำนำ, สารบัญ (ไม่นับรวมเกณฑ์ 25 หน้า) | *(2–3 หน้า)* | รายชื่อสมาชิก 6–8 คน, โลโก้ KMUTT |
| **บทที่ 1** | บทนำ วัตถุประสงค์ และขอบเขตโครงงาน | 1.5 หน้า | เป้าหมาย 4 ตัวชี้วัดจากโจทย์ |
| **บทที่ 2** | ทฤษฎีพื้นฐาน: CPU Virtualization, EEVDF Scheduler และ Kernel Tracepoints | 5.0 หน้า | แผนภาพ Context Switch / Wakeup / Migration, `kernel/tracepoint.c`, `DECLARE_TRACE` vs `TRACE_EVENT` |
| **บทที่ 3** | การออกแบบระบบ Process Scheduler Profiler | 4.0 หน้า | System Architecture Diagram, State/Sync Design, การแก้บั๊ก `sched_migrate_task` & Linux 7.0 APIs |
| **บทที่ 4** | ขั้นตอนการพัฒนาและ Re-build Kernel / Kernel Module | 4.5 หน้า | ภาพ `m01`–`m05` และ in-tree build `m13`–`m16` (รวมปัญหา BTF mismatch และวิธีแก้) |
| **บทที่ 5** | การทดลองและทวนสอบผล (Workloads & Cross-Validation) | 5.5 หน้า | ภาพ `m06`–`m12`, ตารางเปรียบเทียบกับ `perf` และ `ftrace` พร้อมวิเคราะห์เหตุผลความแตกต่าง |
| **บทที่ 6** | ปัญหาที่พบ แนวทางแก้ไข และสรุปผล | 2.0 หน้า | สรุป 5 บั๊กที่ค้นพบและแก้ + ข้อเสนอแนะ |
| **รวมเนื้อหาหลัก** | **บทที่ 1 – บทที่ 6** | **22.5 หน้า** | **อยู่ภายในขีดจำกัด 25 หน้าอย่างปลอดภัย** |
| *ภาคผนวก* | ภาคผนวก A–D (ไม่นับในเกณฑ์ 25 หน้า) | *(ตามความเหมาะสม)* | โค้ด `sched_profiler.c`, Workloads C, สคริปต์ Validation, Diff In-tree Kernel |
