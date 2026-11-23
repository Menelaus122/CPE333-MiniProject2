# Ultra-Presentation Plan: Process Scheduler Profiler (7–11 นาที)

แผนการนำเสนอสำหรับสมาชิก **6–8 คน** ตามกติกาใน [`mini_project_2_cpu_virtualization.md`](../mini_project_2_cpu_virtualization.md):
- **เวลา:** 7–11 นาที (**ตั้งเป้าซ้อมไว้ที่ 9 นาที 30 วินาที** เผื่อเวลา Q&A/สลับไมค์)
- **รูปแบบ:** *Fancy presentation is allowed* — เน้น Storyline แบบ **"Kernel Detective"** (นักสืบเคอร์เนลแกะรอยชีวิตของโปรเซส 1 ตัวใน 1 วินาที)

---

## 1. โครงสร้างสไลด์ (12 สไลด์ — 9:30 นาที)

| สไลด์ | เวลา | เนื้อหาหลัก | แนวคิดภาพ / Animation (*Fancy Element*) |
|---|---:|---|---|
| **1. Title & Hook** | 0:30 | ชื่อโปรเจกต์, คำถามเปิด: *"รู้ได้อย่างไรว่า 1 วินาทีโปรเซสถูกเตะออกจาก CPU กี่ครั้ง?"* | Animated Timeline ของ CPU 12 Cores ที่สลับสี Task ตลอดเวลา |
| **2. Objectives & Metrics** | 0:45 | เป้าหมาย 4 ตัวชี้วัด: `Sched In`, `Sched Out`, `Woken Up`, `CPU Migrations` | 4 Metric Cards ที่เปิดไฟทีละใบตามวงจรชีวิตโปรเซส |
| **3. Kernel Tracepoints Under the Hood** | 1:15 | `kernel/tracepoint.c`: Static Key (`NOP` -> Binary Patch), `__tracepoints_ptrs`, RCU | ภาพเปรียบเทียบ 1 คำสั่ง `NOP` vs Active Hook + RCU Grace Period |
| **4. `DECLARE_TRACE` vs `TRACE_EVENT`** | 1:00 | ความต่าง 3 ระดับ (`DECLARE_TRACE`, `TRACE_EVENT`, `DECLARE_EVENT_CLASS`), `CREATE_TRACE_POINTS` | แผนภาพ Macro Expansion ฝั่งซ้าย–ขวา + ตัวอย่างจาก `sched.h` |
| **5. System Architecture** | 1:00 | Kernel Module + `/proc/sched_profiler` + Dual CLI/Proc Input + Lock-Free Counters | Architecture Diagram แบบ Animated Flow (User Space <-> Kernel Space) |
| **6. War Stories: 3 Bugs We Caught!** | 1:15 | ไฮไลต์ทางเทคนิค: (1) `sched_migrate_task` ไม่มี `orig_cpu`, (2) แยก voluntary/involuntary ด้วย `preempt` อย่างเดียวผิด ต้องดู `prev_state` ด้วย (ผลตรงกับ `nivcsw`), (3) `perf stat -p` พลาด off-CPU wakeup | สไลด์ "Bug Bounty" แสดงโค้ดผิด vs โค้ดที่แก้จริงพร้อมผลกระทบ |
| **7. Kernel Build & Integration** | 0:45 | Out-of-Tree LKM (`sched_profiler.ko`) & In-Tree Kernel Rebuild (`drivers/misc/Kconfig`) | ลิงก์ต่อยอดจาก Mini-Project 1 (`7.0.0-999-generic`) |
| **8. LIVE DEMO (หรือ Video Backup)** | 2:00 | สาธิตจริงทั้ง 3 Workloads: CPU `--compete`, Sleep `40x50ms`, Migration + `taskset` | จอ Terminal Split: ซ้ายรัน `sched_profiler_ctl`, ขวา `watch -n 0.2 cat /proc/sched_profiler` |
| **9. Validation vs `perf` & `ftrace`** | 0:45 | ตารางเปรียบเทียบ 3 Workloads ระหว่าง Profiler ของกลุ่มกับ `perf record -a` และ `ftrace` | กราฟแท่งเปรียบเทียบ Diff ~ 0% พร้อมอธิบายข้อจำกัดของ `perf -p` |
| **10. Summary & Takeaways** | 0:30 | สรุปสิ่งที่ได้เรียนรู้ (CPU Virtualization, Lock-free RCU, Kernel Debugging) + ปิดท้าย | QR Code ไปยัง GitHub Repository / Report |

---

## 2. การแบ่งบทบาทสมาชิก (รองรับ 6–8 คน)

| บทบาท | กลุ่ม 6 คน | กลุ่ม 7–8 คน | หน้าที่ช่วงนำเสนอ |
|---|---|---|---|
| **Presenter 1: Intro & Hook** | คนที่ 1 (Slide 1–2) | คนที่ 1 (Slide 1–2) | เปิดเรื่อง ดึงความสนใจ อธิบาย 4 Metrics |
| **Presenter 2: Tracepoint Internals** | คนที่ 2 (Slide 3–4) | คนที่ 2 (Slide 3) + คนที่ 3 (Slide 4) | อธิบาย `kernel/tracepoint.c` และ `DECLARE_TRACE` vs `TRACE_EVENT` |
| **Presenter 3: Architecture & War Stories** | คนที่ 3 (Slide 5–6) | คนที่ 4 (Slide 5) + คนที่ 5 (Slide 6) | อธิบายสถาปัตยกรรม + โชว์บั๊กที่แก้บน Linux 7.0 |
| **Demo Driver (ผู้กดหน้าจอ)** | คนที่ 4 (Slide 7–8) | คนที่ 6 (Slide 7–8) | พิมพ์/รันคำสั่ง Demo บน VM `CPE333-kernel` |
| **Demo Narrator (ผู้บรรยายสด)** | คนที่ 5 (Slide 8 คู่กัน) | คนที่ 7 (Slide 8 คู่กัน) | ชี้ตัวเลขบนหน้าจอให้อาจารย์เห็น (Sched In/Out/Wakeup/Migrate) |
| **Validation & Conclusion** | คนที่ 6 (Slide 9–10) | คนที่ 8 (Slide 9–10) | สรุปผลเทียบกับ `perf`/`ftrace` และคุมช่วง Q&A |

---

## 3. Cue Sheet สำหรับช่วง Live Demo (2 นาที)

เตรียม Terminal ใน VM `CPE333-kernel` ให้ตัวอักษรใหญ่ (อย่างน้อย 18–20 pt) ก่อนขึ้นเวที:

1. **ตรวจสอบ Kernel & Load Module (15 วินาที):**
   ```bash
   uname -r
   sudo insmod ~/miniproj2/kernel/sched_profiler.ko
   ```
2. **Workload 1 — CPU-Bound + Compete (35 วินาที):**
   ```bash
   ./workloads/sched_profiler_ctl --run ./workloads/cpu_workload 2 --compete
   ```
   *จุดชี้บนจอ:* ค่า `Scheduled Out (involuntary)` พุ่งสูงเพราะแย่งกันใช้ 1 Core และตรงกับ `nivcsw` ในบรรทัด `self-check`
3. **Workload 2 — Sleep/Wakeup (35 วินาที):**
   ```bash
   ./workloads/sched_profiler_ctl --run ./workloads/sleep_workload 20 50
   ```
   *จุดชี้บนจอ:* ค่า `Woken Up` ใกล้เคียง `20` และ `Scheduled Out (voluntary)` ตรงกับจำนวนรอบ
4. **Workload 3 — Multi-Core Migration (35 วินาที):**
   ```bash
   ./workloads/sched_profiler_ctl --run ./workloads/migrate_workload 20 30 --mode affinity
   ```
   *จุดชี้บนจอ:* ค่า `CPU Migrations` และ `Last Migration: CPU X -> CPU Y`

> **แผนสำรอง (Fallback Plan):** อัดวิดีโอหน้าจอ Live Demo ความละเอียด Full HD แบบไม่ตัดต่อเก็บไว้ล่วงหน้า 1 ชุด ฝังไว้ในสไลด์ถัดไปหรือเปิดจาก Desktop ทันทีหากโปรเจกเตอร์/VM สะดุด

---

## 4. คลังคำตอบเตรียมพร้อมช่วง Q&A

1. **Q: ทำไมไม่ใช้ `EXPORT_TRACEPOINT_SYMBOL_GPL` ตรงๆ กับ `register_trace_sched_switch()`?**
   - **A:** สัญลักษณ์ `__tracepoint_sched_switch` ไม่ได้ถูก export ให้ทุกโมดูลในหลาย Kernel config การใช้ `for_each_kernel_tracepoint()` ค้นหา `struct tracepoint *` ด้วยชื่อแล้วลงทะเบียนผ่าน `tracepoint_probe_register()` จึงทำงานได้โดยไม่ต้องพึ่ง symbol export เฉพาะของแต่ละ tracepoint (แต่ต้องใช้ `MODULE_LICENSE("GPL")`)
2. **Q: ทำไมต้องใช้ `atomic64_t` และ `synchronize_rcu()` ทั้งคู่?**
   - **A:** `atomic64_t` ป้องกัน Race Condition ขณะ Tracepoint ยิงพร้อมกันหลาย CPU Core; ส่วน `synchronize_rcu()` ตอนเปลี่ยน PID/Reset ป้องกันกรณี Callback บน Core อื่นที่ยังทำงานค้างอยู่มาเขียนค่าเก่าทับตัวนับหลังถูก Reset เป็น 0
3. **Q: ทำไม `perf stat -p <PID>` ถึงได้ค่า `sched_wakeup` เป็น 0 หรือน้อยกว่าความเป็นจริง?**
   - **A:** เพราะ `-p <PID>` กรองเฉพาะขณะที่โปรเซสนั้นเป็น `current` แต่ `sched_wakeup` เกิดขึ้นโดยโปรเซสอื่น (เช่น timer/IRQ/waker) ขณะโปรเซสเป้าหมายยังหลับอยู่ จึงต้องใช้ `perf record -a` พร้อม `--filter "pid == <PID>"` จึงจะได้ตัวเลขที่ถูกต้องตรงกับ Profiler ของเรา
4. **Q: `sched_switch` นับตาม `pid` หรือ `tgid`? ถ้าโปรแกรมมีหลาย Thread จะเป็นอย่างไร?**
   - **A:** ใน Kernel ของ Linux แต่ละ Thread มี `task_struct` และ `pid` (TID) แยกกัน แต่ใช้ `tgid` ร่วมกัน Profiler ของเรารองรับทั้งโหมด `pid <N>` (ดีฟอลต์, นับราย Thread ตรงกับ `ftrace`/`perf`) และโหมด `tgid <N>` (รวมทุก Thread ของโปรเซส)
5. **Q: ต่างจาก `DECLARE_TRACE` และ `TRACE_EVENT` อย่างไร?**
   - **A:** `DECLARE_TRACE` สร้างเพียงจุด Hook เปล่าสำหรับให้ Callback มาเกาะ ส่วน `TRACE_EVENT` สร้างบน `DECLARE_TRACE` เพื่อเพิ่ม Ring Buffer entry, `TP_fast_assign` และ `TP_printk` ให้ `ftrace`/`perf` ใช้งานได้ทันที
