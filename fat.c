/*
 * fat.c - FAT12 LAB
 * Bien dich:  gcc -O2 -o fat.exe fat.c        (MinGW / Linux)
 *             cl fat.c /Fe:fat.exe            (MSVC)
 * Su dung:
 *   fat.exe <image> 1               : Boot Sector / BPB
 *   fat.exe <image> 2               : Noi dung FAT cua cluster 2..101
 *   fat.exe <image> 3               : Liet ke ROOT directory (nhu DIR)
 *   fat.exe <image> 4 <TEN.EXT>     : Cluster chain cua file o ROOT
 *   fat.exe <image> 5               : Dem cluster trong trong cluster 2..101
 *
 * Chuong trinh CHI DOC file image (mo o che do "rb"), khong ghi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------- Doc so little-endian ---------- */
static u16 rd16(const u8 *p) { return (u16)(p[0] | (p[1] << 8)); }
static u32 rd32(const u8 *p) { return (u32)(p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24)); }

/* ---------- Thong tin BPB ---------- */
typedef struct {
    char oem[9];
    u16  bytesPerSector;
    u8   sectorsPerCluster;
    u16  reservedSectors;
    u8   numFATs;
    u16  rootEntries;
    u16  totalSectors16;
    u8   media;
    u16  sectorsPerFAT;
    u16  sectorsPerTrack;
    u16  numHeads;
    u32  hiddenSectors;
    u32  totalSectors32;
    u8   driveNumber;
    u8   bootSig;
    u32  volumeID;
    char label[12];
    char fsType[9];

    /* Cac gia tri suy ra */
    u32  totalSectors;
    u32  fatStartSector;
    u32  rootStartSector;
    u32  rootSectors;
    u32  dataStartSector;
    u32  dataSectors;
    u32  totalClusters;   /* so data cluster (bat dau tu cluster 2) */
} BPB;

static int readSector(FILE *f, const BPB *b, u32 lba, u8 *buf) {
    if (fseek(f, (long)lba * b->bytesPerSector, SEEK_SET) != 0) return 0;
    return fread(buf, 1, b->bytesPerSector, f) == b->bytesPerSector;
}

static int readBPB(FILE *f, BPB *b) {
    u8 bs[512];
    memset(b, 0, sizeof(*b));
    fseek(f, 0, SEEK_SET);
    if (fread(bs, 1, 512, f) != 512) return 0;

    memcpy(b->oem, bs + 3, 8);       b->oem[8] = 0;
    b->bytesPerSector   = rd16(bs + 11);
    b->sectorsPerCluster= bs[13];
    b->reservedSectors  = rd16(bs + 14);
    b->numFATs          = bs[16];
    b->rootEntries      = rd16(bs + 17);
    b->totalSectors16   = rd16(bs + 19);
    b->media            = bs[21];
    b->sectorsPerFAT    = rd16(bs + 22);
    b->sectorsPerTrack  = rd16(bs + 24);
    b->numHeads         = rd16(bs + 26);
    b->hiddenSectors    = rd32(bs + 28);
    b->totalSectors32   = rd32(bs + 32);
    b->driveNumber      = bs[36];
    b->bootSig          = bs[38];
    b->volumeID         = rd32(bs + 39);
    memcpy(b->label, bs + 43, 11);   b->label[11] = 0;
    memcpy(b->fsType, bs + 54, 8);   b->fsType[8] = 0;

    if (b->bytesPerSector == 0 || b->sectorsPerCluster == 0 || b->numFATs == 0) {
        fprintf(stderr, "BPB khong hop le.\n");
        return 0;
    }

    b->totalSectors    = b->totalSectors16 ? b->totalSectors16 : b->totalSectors32;
    b->fatStartSector  = b->reservedSectors;
    b->rootStartSector = b->reservedSectors + (u32)b->numFATs * b->sectorsPerFAT;
    b->rootSectors     = ((u32)b->rootEntries * 32 + b->bytesPerSector - 1) / b->bytesPerSector;
    b->dataStartSector = b->rootStartSector + b->rootSectors;
    b->dataSectors     = b->totalSectors - b->dataStartSector;
    b->totalClusters   = b->dataSectors / b->sectorsPerCluster;
    return 1;
}

/* ---------- Doc bang FAT thu nhat vao bo nho ---------- */
static u8 *loadFAT(FILE *f, const BPB *b) {
    u32 size = (u32)b->sectorsPerFAT * b->bytesPerSector;
    u8 *fat = (u8 *)malloc(size);
    if (!fat) return NULL;
    if (fseek(f, (long)b->fatStartSector * b->bytesPerSector, SEEK_SET) != 0 ||
        fread(fat, 1, size, f) != size) {
        free(fat);
        return NULL;
    }
    return fat;
}

/* ---------- Giai ma FAT12: entry n nam o offset n*1.5 ---------- */
static u16 fat12Get(const u8 *fat, u32 n) {
    u32 off = n + n / 2;            /* n * 3 / 2 */
    u16 v = rd16(fat + off);
    if (n & 1) v >>= 4;             /* cluster le: lay 12 bit cao  */
    else       v &= 0x0FFF;         /* cluster chan: lay 12 bit thap */
    return v;
}

static const char *fatEntryMeaning(u16 v) {
    if (v == 0x000)               return "FREE";
    if (v == 0xFF7)               return "BAD";
    if (v >= 0xFF8)               return "EOF";
    if (v == 0x001 || (v >= 0xFF0 && v <= 0xFF6)) return "RESERVED";
    return "NEXT";
}

/* ---------- Ten 8.3 ---------- */
static void formatName83(const u8 *e, char *out) {
    char name[9], ext[4];
    int i, n = 8, x = 3;
    memcpy(name, e, 8); memcpy(ext, e + 8, 3);
    while (n > 0 && name[n - 1] == ' ') n--;
    while (x > 0 && ext[x - 1] == ' ') x--;
    name[n] = 0; ext[x] = 0;
    if (x) sprintf(out, "%s.%s", name, ext);
    else   strcpy(out, name);
    for (i = 0; out[i]; i++) out[i] = (char)toupper((unsigned char)out[i]);
}

static void normalizeInputName(const char *in, char *out) {
    /* Chuan hoa ten nguoi dung nhap ve dang NAME.EXT viet hoa */
    int i;
    for (i = 0; in[i] && i < 12; i++) out[i] = (char)toupper((unsigned char)in[i]);
    out[i] = 0;
}

/* ====================================================================== */
/* 1. Boot sector                                                         */
/* ====================================================================== */
static void cmdBoot(const BPB *b) {
    printf("=== BOOT SECTOR / BPB ===\n");
    printf("OEM ID                : %s\n", b->oem);
    printf("Bytes per sector      : %u\n", b->bytesPerSector);
    printf("Sectors per cluster   : %u\n", b->sectorsPerCluster);
    printf("Reserved sectors      : %u\n", b->reservedSectors);
    printf("Number of FATs        : %u\n", b->numFATs);
    printf("Root entries          : %u\n", b->rootEntries);
    printf("Total sectors (16)    : %u\n", b->totalSectors16);
    printf("Media descriptor      : 0x%02X\n", b->media);
    printf("Sectors per FAT       : %u\n", b->sectorsPerFAT);
    printf("Sectors per track     : %u\n", b->sectorsPerTrack);
    printf("Number of heads       : %u\n", b->numHeads);
    printf("Hidden sectors        : %u\n", b->hiddenSectors);
    printf("Total sectors (32)    : %u\n", b->totalSectors32);
    printf("Drive number          : 0x%02X\n", b->driveNumber);
    printf("Boot signature        : 0x%02X\n", b->bootSig);
    printf("Volume ID             : 0x%08X\n", b->volumeID);
    printf("Volume label          : \"%s\"\n", b->label);
    printf("File system type      : \"%s\"\n", b->fsType);
    printf("--- Gia tri suy ra ---\n");
    printf("Total sectors         : %u\n", b->totalSectors);
    printf("Disk size             : %u bytes (%.2f KB)\n",
           b->totalSectors * b->bytesPerSector,
           b->totalSectors * b->bytesPerSector / 1024.0);
    printf("FAT #1 start sector   : %u\n", b->fatStartSector);
    printf("ROOT dir start sector : %u (%u sectors)\n", b->rootStartSector, b->rootSectors);
    printf("DATA start sector     : %u\n", b->dataStartSector);
    printf("Data clusters         : %u (cluster 2..%u)\n", b->totalClusters, b->totalClusters + 1);
}

/* ====================================================================== */
/* 2. Noi dung FAT cua 100 data cluster dau tien (2..101)                  */
/* ====================================================================== */
static void cmdFAT(const BPB *b, const u8 *fat) {
    u32 c, last = 101;
    if (last > b->totalClusters + 1) last = b->totalClusters + 1;
    printf("=== FAT12 - ENTRY CUA CLUSTER 2..%u ===\n", last);
    printf("FAT[0] = 0x%03X (media)   FAT[1] = 0x%03X\n\n", fat12Get(fat, 0), fat12Get(fat, 1));
    printf("Cluster  Value   Meaning\n");
    printf("-------  -----   -------\n");
    for (c = 2; c <= last; c++) {
        u16 v = fat12Get(fat, c);
        printf("%7u  0x%03X   %s", c, v, fatEntryMeaning(v));
        if (strcmp(fatEntryMeaning(v), "NEXT") == 0) printf(" -> %u", v);
        printf("\n");
    }
}

/* ====================================================================== */
/* 3. ROOT directory nhu lenh DIR                                          */
/* ====================================================================== */
static void printDate(u16 d) {
    printf("%02u/%02u/%04u", d & 0x1F, (d >> 5) & 0x0F, 1980 + (d >> 9));
}
static void printTime(u16 t) {
    printf("%02u:%02u:%02u", t >> 11, (t >> 5) & 0x3F, (t & 0x1F) * 2);
}

static void cmdRoot(FILE *f, const BPB *b) {
    u8 *sec = (u8 *)malloc(b->bytesPerSector);
    u32 s, i, nFiles = 0, nDirs = 0, totalBytes = 0;
    int done = 0;

    printf("=== ROOT DIRECTORY (Volume label: %s) ===\n\n", b->label);
    printf("%-12s  %-5s  %10s  %-10s %-8s  %s\n", "Name", "Attr", "Size", "Date", "Time", "1st cluster");
    printf("%-12s  %-5s  %10s  %-10s %-8s  %s\n", "------------", "-----", "----------",
           "----------", "--------", "-----------");

    for (s = 0; s < b->rootSectors && !done; s++) {
        if (!readSector(f, b, b->rootStartSector + s, sec)) break;
        for (i = 0; i < (u32)b->bytesPerSector / 32; i++) {
            const u8 *e = sec + i * 32;
            u8 attr = e[11];
            char name[13], attrs[6];

            if (e[0] == 0x00) { done = 1; break; }   /* het entry */
            if (e[0] == 0xE5) continue;              /* entry da xoa */
            if (attr == 0x0F) continue;              /* LFN entry - bo qua */

            formatName83(e, name);
            attrs[0] = (attr & 0x01) ? 'R' : '-';
            attrs[1] = (attr & 0x02) ? 'H' : '-';
            attrs[2] = (attr & 0x04) ? 'S' : '-';
            attrs[3] = (attr & 0x10) ? 'D' : '-';
            attrs[4] = (attr & 0x20) ? 'A' : '-';
            attrs[5] = 0;

            if (attr & 0x08) {                       /* volume label */
                printf("%-12s  %-5s  %10s  ", name, "VOL", "<LABEL>");
                printDate(rd16(e + 24)); printf(" "); printTime(rd16(e + 22));
                printf("\n");
                continue;
            }
            if (attr & 0x10) {
                printf("%-12s  %-5s  %10s  ", name, attrs, "<DIR>");
                nDirs++;
            } else {
                u32 size = rd32(e + 28);
                printf("%-12s  %-5s  %10u  ", name, attrs, size);
                nFiles++; totalBytes += size;
            }
            printDate(rd16(e + 24)); printf(" "); printTime(rd16(e + 22));
            printf("  %u\n", rd16(e + 26));
        }
    }
    printf("\n%10u File(s)  %u bytes\n%10u Dir(s)\n", nFiles, totalBytes, nDirs);
    free(sec);
}

/* ====================================================================== */
/* 4. Tim cluster chain cua mot file o ROOT                                */
/* ====================================================================== */
static int findRootEntry(FILE *f, const BPB *b, const char *want, u8 *outEntry) {
    u8 *sec = (u8 *)malloc(b->bytesPerSector);
    u32 s, i;
    int found = 0;
    for (s = 0; s < b->rootSectors && !found; s++) {
        if (!readSector(f, b, b->rootStartSector + s, sec)) break;
        for (i = 0; i < (u32)b->bytesPerSector / 32; i++) {
            const u8 *e = sec + i * 32;
            char name[13];
            if (e[0] == 0x00) { s = b->rootSectors; break; }
            if (e[0] == 0xE5 || e[11] == 0x0F || (e[11] & 0x08)) continue;
            formatName83(e, name);
            if (strcmp(name, want) == 0) { memcpy(outEntry, e, 32); found = 1; break; }
        }
    }
    free(sec);
    return found;
}

static void cmdChain(FILE *f, const BPB *b, const u8 *fat, const char *fname) {
    char want[16];
    u8 e[32];
    u32 cur, count = 0, size, expect;
    u32 maxIter = b->totalClusters + 2;   /* chong vong lap vo han */

    normalizeInputName(fname, want);
    if (!findRootEntry(f, b, want, e)) {
        printf("Khong tim thay file \"%s\" trong ROOT.\n", want);
        return;
    }
    size = rd32(e + 28);
    cur  = rd16(e + 26);
    expect = (size + (u32)b->sectorsPerCluster * b->bytesPerSector - 1) /
             ((u32)b->sectorsPerCluster * b->bytesPerSector);

    printf("=== CLUSTER CHAIN CUA \"%s\" ===\n", want);
    printf("Size          : %u bytes\n", size);
    printf("First cluster : %u\n", cur);
    printf("Cluster can   : %u (theo kich thuoc)\n\n", expect);

    if (e[11] & 0x10) printf("(Day la thu muc)\n");
    if (cur < 2) { printf("File rong, khong co cluster nao.\n"); return; }

    printf("Chain: ");
    while (cur >= 2 && cur < 0xFF8 && count < maxIter) {
        u16 next;
        u32 lba = b->dataStartSector + (cur - 2) * b->sectorsPerCluster;
        if (count) printf(" -> ");
        printf("%u", cur);
        count++;
        next = fat12Get(fat, cur);
        if (next == 0xFF7) { printf(" [BAD]"); break; }
        if (next == 0x000) { printf(" [FREE?! chain hong]"); break; }
        (void)lba;
        cur = next;
    }
    if (cur >= 0xFF8) printf(" -> EOF");
    printf("\n\nTong so cluster : %u\n", count);

    /* In them vi tri sector cua tung cluster */
    printf("\n%-8s %-12s %s\n", "STT", "Cluster", "Sector (LBA)");
    cur = rd16(e + 26); count = 0;
    while (cur >= 2 && cur < 0xFF8 && count < maxIter) {
        u32 lba = b->dataStartSector + (cur - 2) * b->sectorsPerCluster;
        u16 next = fat12Get(fat, cur);
        printf("%-8u %-12u %u", count + 1, cur, lba);
        if (b->sectorsPerCluster > 1) printf("..%u", lba + b->sectorsPerCluster - 1);
        printf("\n");
        count++;
        if (next == 0xFF7 || next == 0x000) break;
        cur = next;
    }
    if (count != expect)
        printf("\nCANH BAO: so cluster thuc te (%u) khac so cluster theo kich thuoc (%u).\n", count, expect);
}

/* ====================================================================== */
/* 5. Dem cluster trong trong 100 data cluster dau tien                    */
/* ====================================================================== */
static void cmdFree(const BPB *b, const u8 *fat) {
    u32 c, last = 101, freeCnt = 0, usedCnt = 0, badCnt = 0;
    if (last > b->totalClusters + 1) last = b->totalClusters + 1;
    printf("=== THONG KE CLUSTER 2..%u ===\n", last);
    printf("Cac cluster trong: ");
    for (c = 2; c <= last; c++) {
        u16 v = fat12Get(fat, c);
        if (v == 0x000)      { if (freeCnt++) printf(", "); printf("%u", c); }
        else if (v == 0xFF7) badCnt++;
        else                 usedCnt++;
    }
    if (!freeCnt) printf("(khong co)");
    printf("\n\nSo cluster trong : %u\n", freeCnt);
    printf("So cluster dung  : %u\n", usedCnt);
    printf("So cluster bad   : %u\n", badCnt);
    printf("Tong             : %u\n", last - 1);
}

/* ====================================================================== */
static void usage(const char *prog) {
    fprintf(stderr,
        "Su dung:\n"
        "  %s <image> 1              Boot sector / BPB\n"
        "  %s <image> 2              FAT entry cua cluster 2..101\n"
        "  %s <image> 3              Liet ke ROOT directory\n"
        "  %s <image> 4 <TEN.EXT>    Cluster chain cua file o ROOT\n"
        "  %s <image> 5              Dem cluster trong (cluster 2..101)\n",
        prog, prog, prog, prog, prog);
}

int main(int argc, char **argv) {
    FILE *f;
    BPB b;
    u8 *fat = NULL;
    int cmd;

    if (argc < 3) { usage(argv[0]); return 1; }

    f = fopen(argv[1], "rb");          /* chi doc */
    if (!f) { perror("Khong mo duoc image"); return 1; }
    if (!readBPB(f, &b)) { fclose(f); return 1; }

    cmd = atoi(argv[2]);
    if (cmd != 3 && cmd != 1) {
        fat = loadFAT(f, &b);
        if (!fat) { fprintf(stderr, "Khong doc duoc FAT.\n"); fclose(f); return 1; }
    }

    switch (cmd) {
    case 1: cmdBoot(&b); break;
    case 2: cmdFAT(&b, fat); break;
    case 3: cmdRoot(f, &b); break;
    case 4:
        if (argc < 4) { fprintf(stderr, "Thieu ten file.\n"); usage(argv[0]); break; }
        cmdChain(f, &b, fat, argv[3]);
        break;
    case 5: cmdFree(&b, fat); break;
    default: usage(argv[0]); break;
    }

    free(fat);
    fclose(f);
    return 0;
}
