# Mini-Project 2: CPE333 Operating Systems

## 📋 กติกาและสิ่งที่ต้องส่ง (Critical Section)

* **สมาชิกกลุ่ม:** ประมาณ 6–8 คน

* **การศึกษาและพัฒนา:** ศึกษาหัวข้อที่ได้รับมอบหมาย และทำการ Re-build kernel files เพื่อให้ทำงานได้ตามเป้าหมาย

* **การนำเสนอ (Ultra-presentation):**

  * ใช้เวลาในการนำเสนอ 7–11 นาที

  * อธิบายเนื้อหาที่ศึกษา (สามารถนำเสนออย่างสร้างสรรค์/จัดเต็มได้ - *fancy present is allowed*)

  * สาธิตการทำงานของระบบที่พัฒนาขึ้น (Demo your implementation)

* **รายงาน (Report):** ความยาวไม่เกิน 25 หน้า (ไม่รวมภาคผนวก / excluding appendix)

## 💻 หัวข้อที่ได้รับ: CPU Virtualization (Process Scheduler Profiler)

### 🎯 เป้าหมายหลัก (Objectives)

สร้างเครื่องมือ **Process Scheduler Profiler** เพื่อสังเกตการณ์ว่า Linux จัดตารางเวลา (schedule) และย้ายการทำงาน (migrate) ของโปรเซสอย่างไร โดยมีรายละเอียดการทำงานดังนี้:

1. **รับค่า Input:** รับค่า `process_id` (PID) ของโปรเซสที่ต้องการติดตาม (ผ่านทาง `/proc` หรือ command line arguments)

2. **ติดตามการทำงาน (Monitoring):** ตรวจสอบกิจกรรมการจัดตารางเวลา (scheduling activities) ของโปรเซสนั้น

3. **แสดงผลลัพธ์ (Output Metrics):**

   * จำนวนครั้งที่โปรเซสถูก **Scheduled In** และ **Scheduled Out**

   * จำนวนครั้งที่โปรเซสถูกปลุกให้ตื่น (**Woken up**)

   * จำนวนครั้งที่โปรเซสย้ายข้ามแกนประมวลผล (**Moves between CPU cores**)

### 💡 คำแนะนำและแหล่งข้อมูลเพิ่มเติม (Hints & Resources)

* ศึกษาเครื่องมืออย่าง `ftrace` และ `perf` เพื่อดูแนวทางการทำงาน

* สำหรับการเขียน Kernel Module ให้ศึกษาเกี่ยวกับ **Tracepoints** ต่อไปนี้:

  * `sched_wakeup`

  * `sched_switch`

  * `sched_migrate_task`

* **Source Code อ้างอิง (Tracepoint):** [Linux Kernel `tracepoint.c`](https://github.com/torvalds/linux/blob/master/kernel/tracepoint.c)

* **เอกสารอธิบายเพิ่มเติม:** [Linux Kernel Tracepoints Documentation](https://docs.kernel.org/trace/tracepoints.html)

* ศึกษาความแตกต่างและการใช้งานระหว่าง `DECLARE_TRACE` และ `TRACE_EVENT`