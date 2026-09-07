FAT12 LAB - FILE HUONG DAN CHO SINH VIEN

File duoc cung cap:
  disk_public.img

Dinh dang:
  FAT12, floppy image 1.44 MB

Yeu cau:
  1. Doc Boot Sector va hien thi cac truong BPB co ban.
  2. Doc FAT va in noi dung 100 data-cluster dau tien (cluster 2..101).
  3. Doc ROOT directory va hien thi noi dung tuong tu lenh DIR.
  4. Nhap ten mot file o ROOT va tim toan bo cluster chain cua file.
  5. Dem so cluster trong trong 100 data-cluster dau tien.

Quy dinh:
  - Chuong trinh chi duoc READ file image, khong duoc ghi thay doi.
  - Khong hard-code ten file disk_public.img.
  - Ten image phai nhan tu tham so dong lenh.
  - Khong hard-code vi tri cluster/ket qua.
  - FAT12 entry phai duoc giai ma dung theo 12-bit.

Goi y giao dien:
  fat.exe disk_public.img 1
  fat.exe disk_public.img 2
  fat.exe disk_public.img 3
  fat.exe disk_public.img 4 BIGFILE.BIN
  fat.exe disk_public.img 5

Luu y:
  "100 cluster dau tien" trong bai nay duoc hieu la 100 DATA CLUSTERS dau tien:
  cluster 2 den cluster 101.
