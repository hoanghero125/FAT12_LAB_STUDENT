# FAT12 LAB – Báo cáo

## 1. Chương trình
- File nguồn: `fat.c` (C chuẩn, chỉ mở image ở chế độ `"rb"` – không ghi).
- Biên dịch: `gcc -O2 -o fat.exe fat.c` (MinGW/Linux) hoặc `cl fat.c /Fe:fat.exe` (MSVC).
- Tên image lấy từ tham số dòng lệnh, không hard-code tên file hay vị trí cluster.

| Lệnh | Chức năng |
|---|---|
| `fat.exe <img> 1` | Đọc Boot Sector, in các trường BPB |
| `fat.exe <img> 2` | In FAT entry của data-cluster 2..101 |
| `fat.exe <img> 3` | Liệt kê ROOT directory kiểu `DIR` |
| `fat.exe <img> 4 <TEN.EXT>` | Tìm toàn bộ cluster chain của file ở ROOT |
| `fat.exe <img> 5` | Đếm cluster trống trong cluster 2..101 |

## 2. Cách thực hiện
- **Boot Sector**: đọc 512 byte đầu, giải mã little-endian các trường tại offset 3, 11, 13, 14, 16, 17, 19, 21, 22, 24, 26, 28, 32, 36, 38, 39, 43, 54. Suy ra: FAT bắt đầu sector `Reserved`; ROOT bắt đầu `Reserved + NumFATs × SectorsPerFAT`; ROOT chiếm `RootEntries × 32 / BytesPerSector` sector; DATA bắt đầu ngay sau ROOT.
- **Giải mã FAT12 (12-bit)**: entry n nằm tại byte offset `n + n/2`. Đọc 16 bit; n chẵn → lấy `& 0x0FFF`; n lẻ → `>> 4`. Ý nghĩa: `0x000` trống, `0xFF7` bad, `0xFF8–0xFFF` EOF, còn lại là cluster kế tiếp.
- **ROOT**: duyệt 14 sector ROOT, mỗi entry 32 byte; bỏ qua entry `0xE5` (đã xóa), entry LFN (attr `0x0F`); dừng ở entry `0x00`. Hiển thị tên 8.3, thuộc tính, kích thước, ngày giờ, cluster đầu.
- **Cluster chain**: tìm entry theo tên (so sánh không phân biệt hoa/thường), lấy first cluster (offset 26), lần theo FAT đến khi gặp EOF; có chống lặp vô hạn và phát hiện chain hỏng/BAD. Sector LBA của cluster c = `DataStart + (c−2) × SectorsPerCluster`.
- **Đếm trống**: đếm entry có giá trị `0x000` trong cluster 2..101.

## 3. Kết quả trên `disk_public.img`
- BPB: 512 B/sector, 1 sector/cluster, 1 reserved, 2 FAT × 9 sector, 224 root entries, 2880 sector, media 0xF0, label `FATLABPUB`, FS `FAT12`. FAT#1 tại sector 1, ROOT tại sector 19 (14 sector), DATA tại sector 33, tổng 2847 data cluster.
- ROOT: `README.TXT` (900 B, cluster 2), `A.TXT` (23 B, cluster 5), `BIGFILE.BIN` (2500 B, cluster 7), `EMPTY.TXT` (0 B), `HELLO.C` (700 B, cluster 25), thư mục `DATA` (cluster 20).
- Chain `BIGFILE.BIN`: **7 → 9 → 12 → 13 → 18 → EOF** (5 cluster, khớp với ⌈2500/512⌉ = 5). File bị phân mảnh (không liên tiếp).
- Chain `HELLO.C`: 25 → 26 → EOF.
- Cluster 2..101: **88 cluster trống**, 11 đang dùng, 1 cluster BAD (cluster 30).

Output đầy đủ của từng lệnh nằm trong `out_1.txt` … `out_5.txt`.
