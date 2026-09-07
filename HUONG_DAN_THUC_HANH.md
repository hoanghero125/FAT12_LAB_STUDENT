# HƯỚNG DẪN THỰC HÀNH CHI TIẾT – BÀI LAB FAT12

Bài lab yêu cầu viết chương trình đọc một image đĩa mềm 1.44 MB định dạng FAT12 và thực hiện 5 chức năng: đọc Boot Sector, đọc FAT, liệt kê ROOT, tìm cluster chain của một file, đếm cluster trống. Tài liệu này hướng dẫn từng bước, kèm cách kiểm tra tay bằng hex dump để bạn hiểu chương trình đang làm gì chứ không chỉ chạy được.

---

## Phần A. Chuẩn bị

### A1. Công cụ cần có
| Việc | Windows | Linux / macOS |
|---|---|---|
| Biên dịch C | MinGW (`gcc`) hoặc Visual Studio (`cl`) | `gcc` / `clang` |
| Xem hex | HxD (miễn phí) | `xxd`, `hexdump -C` |
| Đối chiếu kết quả (tuỳ chọn) | 7-Zip mở được file `.img` | `mtools`: `mdir -i disk_public.img` |

### A2. Cấu trúc thư mục làm việc
```
FAT12_LAB/
├── disk_public.img      ← image được cấp, KHÔNG sửa
├── fat.c                ← mã nguồn
└── fat.exe              ← sinh ra sau khi biên dịch
```

### A3. Biên dịch và chạy thử
```
gcc -Wall -O2 -o fat.exe fat.c
fat.exe disk_public.img 1
```
Nếu in ra bảng BPB với `OEM ID: MSDOS5.0` là môi trường đã ổn.

> Quy định của bài: mở image ở chế độ **chỉ đọc** (`fopen(..., "rb")`), tên image lấy từ `argv[1]`, không hard-code bất kỳ vị trí cluster hay kết quả nào.

---

## Phần B. Kiến thức nền cần nắm

### B1. Bố cục đĩa FAT12 (đĩa mềm 1.44 MB)
```
Sector:  0      1 ... 9    10 ... 18   19 ......... 32   33 ................. 2879
        ┌──────┬──────────┬───────────┬────────────────┬──────────────────────────┐
        │ Boot │  FAT #1  │  FAT #2   │ ROOT directory │        DATA (cluster 2..)│
        └──────┴──────────┴───────────┴────────────────┴──────────────────────────┘
          1 s     9 s        9 s          14 s              2847 s
```
Vị trí từng vùng **không được viết cứng** mà phải tính từ BPB:

- `FAT_start   = ReservedSectors` (= 1)
- `ROOT_start  = ReservedSectors + NumFATs × SectorsPerFAT` (= 1 + 2×9 = 19)
- `ROOT_sectors = ⌈RootEntries × 32 / BytesPerSector⌉` (= 224×32/512 = 14)
- `DATA_start  = ROOT_start + ROOT_sectors` (= 33)
- Cluster **c** (c ≥ 2) nằm tại sector `DATA_start + (c − 2) × SectorsPerCluster`

Lưu ý cluster đầu tiên của vùng DATA đánh số **2**, không phải 0 – vì FAT[0] và FAT[1] được dành riêng.

### B2. Boot Sector – các trường BPB quan trọng
| Offset | Kích thước | Trường | Giá trị trong image |
|---|---|---|---|
| 0x03 | 8 | OEM ID | `MSDOS5.0` |
| 0x0B | 2 | Bytes per sector | 512 |
| 0x0D | 1 | Sectors per cluster | 1 |
| 0x0E | 2 | Reserved sectors | 1 |
| 0x10 | 1 | Number of FATs | 2 |
| 0x11 | 2 | Root entries | 224 |
| 0x13 | 2 | Total sectors (16-bit) | 2880 |
| 0x15 | 1 | Media descriptor | 0xF0 |
| 0x16 | 2 | Sectors per FAT | 9 |
| 0x18 | 2 | Sectors per track | 18 |
| 0x1A | 2 | Number of heads | 2 |
| 0x1C | 4 | Hidden sectors | 0 |
| 0x20 | 4 | Total sectors (32-bit) | 0 |
| 0x26 | 1 | Boot signature | 0x29 |
| 0x27 | 4 | Volume ID | 0x1234ABCD |
| 0x2B | 11 | Volume label | `FATLABPUB  ` |
| 0x36 | 8 | FS type | `FAT12   ` |

Mọi số nhiều byte đều là **little-endian** (byte thấp trước). Ví dụ tại 0x0B có `00 02` → 0x0200 = 512.

### B3. FAT12 – mỗi entry 12 bit (1,5 byte)
Đây là phần dễ sai nhất. Hai entry liên tiếp chiếm 3 byte:

```
byte:     [ b0 ][ b1 ][ b2 ]
entry 2k :  b0  + 4 bit thấp của b1
entry 2k+1: 4 bit cao của b1 + b2
```
Công thức lấy entry **n**:
1. `offset = n + n/2` (chia nguyên, tương đương n × 1,5)
2. Đọc 16 bit little-endian tại offset: `v = fat[offset] | fat[offset+1] << 8`
3. Nếu **n chẵn** → `v & 0x0FFF`; nếu **n lẻ** → `v >> 4`

Ý nghĩa giá trị:
| Giá trị | Ý nghĩa |
|---|---|
| 0x000 | Cluster trống |
| 0x002 – 0xFEF | Cluster kế tiếp trong chain |
| 0xFF7 | Bad cluster |
| 0xFF8 – 0xFFF | Cluster cuối (EOF) |

### B4. Directory entry – 32 byte
| Offset | Kích thước | Ý nghĩa |
|---|---|---|
| 0 | 8 | Tên (đệm dấu cách) |
| 8 | 3 | Phần mở rộng |
| 11 | 1 | Attribute: 0x01 R, 0x02 H, 0x04 S, 0x08 Volume label, 0x10 Dir, 0x20 Archive, **0x0F = LFN** |
| 22 | 2 | Giờ (5 bit giờ / 6 bit phút / 5 bit giây÷2) |
| 24 | 2 | Ngày (7 bit năm+1980 / 4 bit tháng / 5 bit ngày) |
| 26 | 2 | Cluster đầu tiên |
| 28 | 4 | Kích thước file (byte) |

Byte đầu tiên đặc biệt: `0x00` = hết danh sách (dừng duyệt), `0xE5` = entry đã xoá (bỏ qua).

---

## Phần C. Thực hiện từng yêu cầu

Với mỗi yêu cầu: **làm tay trên hex dump trước**, rồi mới đối chiếu với chương trình.

### Yêu cầu 1 – Đọc Boot Sector

**Làm tay.** Mở image bằng HxD, xem 64 byte đầu:
```
00000000  EB 3C 90 4D 53 44 4F 53 35 2E 30 00 02 01 01 00  .<.MSDOS5.0.....
00000010  02 E0 00 40 0B F0 09 00 12 00 02 00 00 00 00 00  ...@............
00000020  00 00 00 00 00 00 29 CD AB 34 12 46 41 54 4C 41  ......)..4.FATLA
00000030  42 50 55 42 20 20 46 41 54 31 32 20 20 20 4E 6F  BPUB  FAT12   No
```
- 0x0B: `00 02` → 512 bytes/sector
- 0x0D: `01` → 1 sector/cluster
- 0x11: `E0 00` → 224 root entries
- 0x13: `40 0B` → 0x0B40 = 2880 sectors
- 0x16: `09 00` → 9 sectors/FAT
- 0x27: `CD AB 34 12` → Volume ID 0x1234ABCD

**Chương trình.** Đọc 512 byte đầu vào mảng `bs[]`, giải mã bằng hàm `rd16()`/`rd32()`, sau đó tính các giá trị suy ra (FAT_start, ROOT_start, DATA_start, số cluster). Chạy `fat.exe disk_public.img 1` và so với bảng B2.

### Yêu cầu 2 – Đọc FAT, in cluster 2..101

**Làm tay.** FAT#1 bắt đầu tại sector 1 = offset 0x200:
```
00000200  F0 FF FF 03 F0 FF 00 F0 FF 00 90 00 00 C0 00 00
00000210  00 00 0D 20 01 00 00 00 00 00 00 FF 0F 00 FF 0F
00000220  00 00 00 00 00 A0 01 FF 0F 00 00 00 00 F7 0F 00
```
Ví dụ giải mã:
- Entry 2 (chẵn): offset = 2 + 1 = 3 → bytes `03 F0` → v = 0xF003 → `& 0xFFF` = **0x003** → cluster 2 nối sang 3
- Entry 3 (lẻ): offset = 3 + 1 = 4 → bytes `F0 FF` → v = 0xFFF0 → `>> 4` = **0xFFF** → EOF
- Entry 7 (lẻ): offset = 7 + 3 = 10 → bytes `90 00` → v = 0x0090 → `>> 4` = **0x009** → cluster 7 nối sang 9
- Entry 30 (chẵn): offset = 30 + 15 = 45 (0x2D) → bytes `F7 0F` → **0xFF7** → BAD

Tự làm thêm entry 9, 12, 13, 18, 25 và ghi lại giá trị.

**Chương trình.** Nạp toàn bộ 9 sector FAT vào bộ nhớ (`loadFAT`), dùng `fat12Get(fat, n)` cho n = 2..101, in giá trị hex và ý nghĩa. Chạy `fat.exe disk_public.img 2` và đối chiếu các entry bạn vừa giải tay.

Lỗi thường gặp: đọc entry theo 2 byte/entry (đó là FAT16) → mọi giá trị sai từ cluster 2 trở đi.

### Yêu cầu 3 – Liệt kê ROOT như lệnh DIR

**Làm tay.** ROOT tại sector 19 = offset 19×512 = 0x2600:
```
00002600  46 41 54 4C 41 42 50 55 20 20 20 08 ...          FATLABPU   attr=08 (label)
00002620  52 45 41 44 4D 45 20 20 54 58 54 20 ...          README  TXT attr=20
00002630  ... 02 00 84 03 00 00                            cluster đầu=2, size=0x384=900
00002640  41 20 20 20 20 20 20 20 54 58 54 20 ...          A       TXT
00002650  ... 05 00 17 00 00 00                            cluster=5, size=23
00002660  42 49 47 46 49 4C 45 20 42 49 4E 20 ...          BIGFILE BIN
00002670  ... 07 00 C4 09 00 00                            cluster=7, size=0x9C4=2500
```

**Chương trình.** Duyệt lần lượt 14 sector ROOT, mỗi sector 16 entry (512/32). Với mỗi entry:
1. Byte 0 = 0x00 → dừng hẳn; = 0xE5 → bỏ qua
2. Attr = 0x0F → entry tên dài, bỏ qua
3. Attr có bit 0x08 → in là volume label
4. Attr có bit 0x10 → in `<DIR>`, ngược lại in kích thước
5. Ghép tên 8.3: cắt dấu cách đệm, thêm dấu chấm nếu có phần mở rộng

Kết quả mong đợi: 5 file (README.TXT, A.TXT, BIGFILE.BIN, EMPTY.TXT, HELLO.C) + 1 thư mục DATA, tổng 4123 byte.

### Yêu cầu 4 – Tìm cluster chain của một file

**Làm tay với BIGFILE.BIN.** Từ ROOT: cluster đầu = 7, size = 2500 → cần ⌈2500/512⌉ = 5 cluster. Lần theo FAT:
```
FAT[7]  = 0x009 → 9
FAT[9]  = 0x00C → 12
FAT[12] = 0x00D → 13
FAT[13] = 0x012 → 18
FAT[18] = 0xFFF → EOF
```
Chain: **7 → 9 → 12 → 13 → 18**. Đếm được 5 cluster, đúng với kích thước. File bị phân mảnh (cluster 8, 10, 11 bị bỏ qua).

**Chương trình.** `fat.exe disk_public.img 4 BIGFILE.BIN`:
1. Chuẩn hoá tên nhập vào (viết hoa) rồi tìm trong ROOT
2. Lấy first cluster tại offset 26 của entry
3. Vòng lặp: in cluster hiện tại → `next = fat12Get(fat, cur)` → dừng khi `next ≥ 0xFF8`
4. Bảo vệ: giới hạn số vòng lặp ≤ tổng số cluster để không treo nếu FAT bị lỗi vòng; dừng nếu gặp 0xFF7 (bad) hoặc 0x000 (chain hỏng)
5. So sánh số cluster đếm được với số cluster theo kích thước, cảnh báo nếu lệch

Thử thêm: `HELLO.C` (25 → 26), `A.TXT` (chỉ 1 cluster), `EMPTY.TXT` (size 0, first cluster 0 → không có chain), và một tên không tồn tại.

### Yêu cầu 5 – Đếm cluster trống trong 2..101

**Làm tay.** Từ bảng FAT ở yêu cầu 2, đếm entry có giá trị 0x000. Cách nhanh: các cluster đang dùng là 2, 3, 5, 7, 9, 12, 13, 18, 20, 25, 26 (11 cluster), cluster 30 là BAD, còn lại trống → 100 − 11 − 1 = **88**.

**Chương trình.** Lặp c = 2..101, đếm `fat12Get(fat, c) == 0`. Nên in kèm số cluster đang dùng và bad để tổng = 100, dễ tự kiểm tra. Chạy `fat.exe disk_public.img 5`.

---

## Phần D. Tự kiểm tra trước khi nộp

- [ ] Chương trình chạy đúng với cả 5 lệnh, tên image truyền qua tham số
- [ ] Đổi tên image (ví dụ `copy disk_public.img test.img`) rồi chạy `fat.exe test.img 3` vẫn đúng
- [ ] Tính MD5/kích thước image trước và sau khi chạy chương trình – phải giống nhau (chứng minh không ghi)
- [ ] Giải mã FAT12 đúng: FAT[2] = 0x003, FAT[3] = 0xFFF, FAT[30] = 0xFF7
- [ ] Chain BIGFILE.BIN = 7 → 9 → 12 → 13 → 18, số cluster khớp kích thước
- [ ] Số cluster trống 2..101 = 88
- [ ] Nhập tên file viết thường (`bigfile.bin`) vẫn tìm được; tên không tồn tại báo lỗi rõ ràng
- [ ] Không có số "ma" trong code: 19, 33, 7, 88… đều phải là kết quả tính toán

---

## Phần E. Câu hỏi mở rộng (tự trả lời trong báo cáo)

1. Vì sao FAT12 dùng 12 bit cho mỗi entry? Số cluster tối đa FAT12 quản lý được là bao nhiêu?
2. FAT #2 dùng để làm gì? Chương trình nên đọc FAT nào khi FAT #1 bị hỏng?
3. Nếu file `BIGFILE.BIN` được ghi liên tiếp không phân mảnh thì chain sẽ ra sao? Phân mảnh ảnh hưởng gì đến tốc độ đọc?
4. Tại sao vùng ROOT có kích thước cố định 14 sector còn thư mục con (`DATA`) lại nằm trong vùng dữ liệu?
5. Mở rộng: viết thêm lệnh `6` đọc và in nội dung thư mục con `DATA` (cluster 20) – dùng lại chính hàm liệt kê entry, chỉ khác nguồn dữ liệu là cluster chain thay vì vùng ROOT.
