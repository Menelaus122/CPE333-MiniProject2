# In-Tree Kernel Build (Re-building Kernel Files)

เอกสารนี้อธิบายการนำ `sched_profiler.c` เข้าไปเป็น driver ใน kernel source tree ของ Mini-Project 1 (`~/kernel/linux-hwe-7.0-7.0.0/`) แล้ว build ด้วยระบบ build ของ kernel เอง (Kbuild ผ่าน `debian/rules` ของ Ubuntu) ตามโจทย์ข้อ **"Re-build kernel files เพื่อให้ทำงานได้ตามเป้าหมาย"**

ทำจริงบน VM `CPE333-kernel` เมื่อ 23 พ.ย. 2569 หลักฐานคือภาพ `m13`–`m16` ใน `result/screenshots/`

## ภาพรวม

| ขั้น | คำสั่ง | ภาพ |
|---|---|---|
| 1. ใส่ไฟล์และแก้ `Kconfig`/`Makefile` | `bash ~/miniproj2/kernel/intree_patch.sh .` | `m13_intree_patch.png` |
| 2. Rebuild kernel tree แบบ incremental | `bash ~/miniproj2/kernel/intree_build.sh .` | `m14_intree_build.png` |
| 3. `modprobe` ครั้งแรกถูกปฏิเสธ (BTF ไม่ตรง) | `sudo modprobe sched_profiler`, `dmesg` | `m15_intree_btf_mismatch.png` |
| 4. ตัด BTF ของ module, sign, ติดตั้ง, `modprobe` | ดูข้อ 4 | `m16_intree_modprobe.png` |

ต่างจาก out-of-tree (`make -C kernel`, ภาพ `m03`) ตรงที่ module นี้ build จาก `drivers/misc/` ภายใน tree, เปิดด้วยตัวเลือก `CONFIG_SCHED_PROFILER` ใน `.config` ของ kernel, ได้ `intree: Y` ใน `modinfo` และติดตั้งใน `/lib/modules/7.0.0-999-generic/kernel/drivers/misc/` จึงโหลดด้วย `modprobe` ได้

---

## 1. ใส่ไฟล์เข้า `drivers/misc/` (`intree_patch.sh`)

Script ทำสามอย่าง และรันซ้ำได้โดยไม่เพิ่มซ้ำ:

1. คัดลอก `sched_profiler.c` ไปที่ `drivers/misc/sched_profiler.c`
2. เพิ่ม entry นี้ก่อน `endmenu` ของ `drivers/misc/Kconfig` (menu "Misc devices"):

   ```kconfig
   config SCHED_PROFILER
   	tristate "CPE333 Process Scheduler Profiler (/proc/sched_profiler)"
   	depends on TRACEPOINTS && PROC_FS
   	default m
   	help
   	  Counts how often one PID is scheduled in and out, woken up and
   	  moved between CPUs, using the sched_switch, sched_wakeup and
   	  sched_migrate_task tracepoints. Results are read from
   	  /proc/sched_profiler. CPE333 Operating Systems, Mini-Project 2.
   ```

3. เพิ่มบรรทัดนี้ท้าย `drivers/misc/Makefile`:

   ```makefile
   obj-$(CONFIG_SCHED_PROFILER)	+= sched_profiler.o
   ```

แล้วตั้ง `CONFIG_SCHED_PROFILER=m` ใน `debian/build/build-generic/.config` ด้วย `scripts/config` ต้องตั้งค่าก่อน build เพราะถ้า `.config` ยังไม่มี symbol ใหม่นี้ `syncconfig` จะหยุดถามค่าระหว่าง build ไฟล์ต้นฉบับของ `Kconfig`/`Makefile` เก็บไว้ที่ `~/kernel/orig-drivers-misc/` เพื่อแสดง `diff -u` ในภาพ `m13`

## 2. Rebuild แบบ incremental (`intree_build.sh`)

ใช้ `debian/rules` ของ Ubuntu แทนการเรียก `make` เอง เพราะ `debian/rules` ส่งตัวแปรให้ `make` ครบชุดเดียวกับตอน build ใน Mini-Project 1 (compiler `x86_64-linux-gnu-gcc-13`, `KERNELVERSION=7.0.0-999-generic`, Rust toolchain ฯลฯ) ถ้าตัวแปรใดต่างไป Kbuild จะมองว่า config เปลี่ยนและ compile ใหม่ทั้ง tree

```bash
rm -f debian/stamps/stamp-build-generic
debian/rules "$PWD/debian/stamps/stamp-build-generic"     # rule: $(stampdir)/stamp-build-%
```

- เรียก stamp target ตรงๆ เพราะ `debian/rules build-arch` ใน tree นี้พึ่ง `stamp-install-*` ไม่ใช่ `stamp-build-*` เมื่อลบ stamp ไปแล้ว `build-arch` จึงไม่ build อะไรเลย (พิมพ์แค่ `Debug: build-arch`)
- ผลจริง (`m14`): `build exit status = 0` ใช้เวลา **12 นาที 43 วินาที** compile เพียง **13 objects** (เทียบกับ 106 นาทีของการ build เต็มใน Mini-Project 1) ได้ `drivers/misc/sched_profiler.ko` ที่มี `intree: Y` และ `vermagic: 7.0.0-999-generic`
- ที่ยังใช้เวลาหลายนาทีเพราะ symbol ใหม่ทำให้ `include/generated/*` เปลี่ยน, `vmlinux` ถูก link ใหม่ (`BTF .tmp_vmlinux1`) และ module ทุกตัวได้ BTF ชุดใหม่

## 3. ปัญหาที่พบ: `modprobe` ได้ `Invalid argument` (`m15`)

```
modprobe: ERROR: could not insert 'sched_profiler': Invalid argument
BPF: Invalid name
failed to validate module [sched_profiler] BTF: -22
```

สาเหตุ: เมื่อ `CONFIG_DEBUG_INFO_BTF_MODULES=y` ตัว Kbuild จะสร้าง BTF ของ module (split BTF) โดยอ้าง type ID ของ BTF ใน `vmlinux` ที่เพิ่ง link ใหม่ใน build directory แต่ kernel ที่กำลังรันอยู่ใช้ BTF ของ `vmlinux` ตัวเดิม (hash ใน `m15`: `/sys/kernel/btf/vmlinux` = `95aec91d…` ส่วนตัวที่ build ใหม่ = `4476965e…`) และ Ubuntu ไม่ได้เปิด `CONFIG_MODULE_ALLOW_BTF_MISMATCH` kernel จึงปฏิเสธ module

Module แบบ out-of-tree (`m03`–`m12`) ไม่เจอปัญหานี้ เพราะ headers package ไม่มี `vmlinux` ทำให้ Kbuild ข้ามขั้น BTF ไป (`Skipping BTF generation ... unavailability of vmlinux`)

## 4. แก้ไขและโหลดด้วย `modprobe` (`m16`)

Profiler นี้ไม่ได้ใช้ BPF จึงตัด section `.BTF` ของ module ออกก่อน sign:

```bash
B=debian/build/build-generic
cp $B/drivers/misc/sched_profiler.ko /tmp/sched_profiler.ko
objcopy --remove-section=.BTF /tmp/sched_profiler.ko
$B/scripts/sign-file sha512 $B/certs/signing_key.pem $B/certs/signing_key.x509 /tmp/sched_profiler.ko
sudo install -m 644 /tmp/sched_profiler.ko /lib/modules/$(uname -r)/kernel/drivers/misc/sched_profiler.ko
sudo depmod -a
sudo modprobe sched_profiler        # โหลดได้ (lsmod เห็น sched_profiler)
sudo modprobe -r sched_profiler
```

- ต้องตัด BTF **ก่อน** sign เพราะ `objcopy` เขียนไฟล์ใหม่และจะทำให้ signature ที่ต่อท้ายไฟล์หายไป
- Key ที่ใช้ sign (`certs/signing_key.pem`) คือ key ที่ Kbuild สร้างอัตโนมัติตอน build kernel ใน Mini-Project 1 (`CONFIG_MODULE_SIG_KEY`) ซึ่ง Kbuild ใส่ certificate ของมันไว้ใน keyring ของ kernel ตัวนั้น `modinfo` แสดง `signer: Build time autogenerated kernel key` ถ้า signature ไม่ถูกต้อง kernel จะปฏิเสธ module (`EKEYREJECTED`) แม้ไม่ได้เปิด `CONFIG_MODULE_SIG_FORCE` การที่ `modprobe` สำเร็จจึงยืนยันว่า signature ไม่ถูกปฏิเสธ (ส่วนข้อความ `module verification failed` ใน dmesg จะพิมพ์แค่ครั้งแรกของการบูตจึงใช้ตัดสินไม่ได้)
- `sched_profiler.ko` ยังติดตั้งอยู่ใน `/lib/modules/7.0.0-999-generic/kernel/drivers/misc/` แต่ไม่โหลดเองตอนบูต ถ้าต้องการถอนออก: `sudo rm /lib/modules/$(uname -r)/kernel/drivers/misc/sched_profiler.ko && sudo depmod -a`

---

## ทางเลือก: Build `.deb` ใหม่ทั้งชุดแล้วบูตเคอร์เนลใหม่

ถ้าต้องการให้ `vmlinux` กับ module ตรงกันทั้งหมด (BTF จะตรงกันเอง ไม่ต้องตัดทิ้ง) ให้ package และติดตั้งเคอร์เนลใหม่ ซึ่งใช้เวลาประมาณ 2 ชั่วโมงเหมือน Mini-Project 1

```bash
cd ~/kernel/linux-*/
D=$(sed -n 's/^DEBIAN=//p' debian/debian.env)
# Ubuntu 6.8+ / 7.0 เก็บค่า config เป็น "policy" ในไฟล์ annotations
# (ห้ามต่อท้ายด้วย CONFIG_X=m ตรงๆ เพราะไม่ใช่รูปแบบของไฟล์นี้ ตัว parser จะอ่านไม่ได้)
grep -q "^CONFIG_SCHED_PROFILER " $D/config/annotations || \
  printf "\nCONFIG_SCHED_PROFILER                           policy<{'amd64': 'm'}>\nCONFIG_SCHED_PROFILER                           note<'CPE333 Mini-Project 2'>\n" >> $D/config/annotations
fakeroot debian/rules clean
{ date; time skipabi=true skipmodule=true fakeroot debian/rules binary; rc=$?; date; echo "build exit status = $rc"; } 2>&1 | tee ~/build_mp2_full.log
cd ~/kernel
sudo dpkg -i linux-headers-*-999-*_amd64.deb linux-image-unsigned-*-999-generic_*_amd64.deb linux-modules-*-999-generic_*_amd64.deb
sudo update-grub && sudo reboot
# หลังบูต: sudo modprobe sched_profiler && cat /proc/sched_profiler
```

ข้อควรระวัง: `debian/rules clean` จะลบ build directory 32 GB ที่ใช้ใน incremental build ข้างบน
