/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * ============================================================
 * FILE: mm-memphy.c
 * MỤC ĐÍCH: Mô phỏng thiết bị bộ nhớ vật lý (RAM và SWAP).
 *
 * MÔ HÌNH:
 *   Mỗi thiết bị (memphy_struct) là một mảng byte đơn giản:
 *     storage[0 .. maxsz-1]
 *
 *   Bộ nhớ được chia thành các FRAME có kích thước PAGING_PAGESZ.
 *   Ví dụ: RAM 256MB, PAGESZ=4096 → 256MB/4096 = 65536 frames.
 *
 * HAI CHẾ ĐỘ TRUY CẬP:
 *   rdmflg=1 → Random Access (RAM): truy cập trực tiếp storage[addr]
 *   rdmflg=0 → Sequential Access (SWAP disk): phải di cursor đến addr
 *              trước khi đọc/ghi (mô phỏng đĩa tuần tự)
 *
 * QUẢN LÝ FRAME TRỐNG:
 *   free_fp_list: linked list các framephy_struct lưu FPN của frame trống.
 *   Ban đầu (MEMPHY_format): tạo linked list với TẤT CẢ các frame.
 *   MEMPHY_get_freefp: lấy 1 frame trống (xóa khỏi list)
 *   MEMPHY_put_freefp: trả 1 frame về list trống (thêm vào đầu)
 * ============================================================
 */
#include "mm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* MEMPHY_mv_csr: di chuyển "cursor" của thiết bị tuần tự đến địa chỉ offset.
 * Mô phỏng đầu đọc đĩa phải quay đến vị trí cần đọc/ghi.
 * Chỉ dùng khi rdmflg=0 (sequential device). */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   int numstep = 0;
   mp->cursor = 0;
   /* "quay" cursor từ 0 đến offset theo kiểu vòng tròn */
   while (numstep < offset && numstep < mp->maxsz) {
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }
   return 0;
}

/* MEMPHY_seq_read: đọc từ thiết bị TUẦN TỰ (rdmflg=0, mô phỏng SWAP disk).
 * Phải di cursor đến addr trước khi đọc. */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)              return -1;
   if (mp->rdmflg)              return -1; /* sai chế độ */
   if (addr >= (addr_t)mp->maxsz) return -1; /* địa chỉ vượt giới hạn */
   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[addr];
   return 0;
}

/* MEMPHY_read: đọc 1 byte tại địa chỉ vật lý addr.
 * Tự động chọn chế độ random hoặc sequential theo rdmflg. */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL) return -1;
   if (mp->rdmflg)
      *value = mp->storage[addr]; /* RAM: truy cập trực tiếp */
   else
      return MEMPHY_seq_read(mp, addr, value); /* SWAP: tuần tự */
   return 0;
}

/* MEMPHY_seq_write: ghi vào thiết bị TUẦN TỰ. */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{
   if (mp == NULL)              return -1;
   if (mp->rdmflg)              return -1;
   if (addr >= (addr_t)mp->maxsz) return -1;
   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;
   return 0;
}

/* MEMPHY_write: ghi 1 byte vào địa chỉ vật lý addr. */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL) return -1;
   if (mp->rdmflg)
      mp->storage[addr] = data; /* RAM */
   else
      return MEMPHY_seq_write(mp, addr, data); /* SWAP */
   return 0;
}

/* MEMPHY_format: khởi tạo danh sách frame trống (free_fp_list).
 * Tính số frame = maxsz / pagesz.
 * Tạo linked list: frame 0 → frame 1 → ... → frame (numfp-1) → NULL
 *
 * Ví dụ: RAM=256MB, PAGESZ=4096 → numfp=65536 frames (fpn 0..65535)
 *
 * Sau khi format, tất cả frame đều "trống" và sẵn sàng cấp phát. */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   int numfp = mp->maxsz / pagesz; /* số frame */
   struct framephy_struct *newfst, *fst;
   int iter = 0;

   if (numfp <= 0) return -1;

   /* frame đầu tiên (fpn=0) */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   mp->free_fp_list = fst;

   /* tạo các frame còn lại */
   for (iter = 1; iter < numfp; iter++) {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }
   return 0;
}

/* MEMPHY_get_freefp: lấy ra 1 frame trống từ đầu free_fp_list.
 * Trả về FPN qua con trỏ retfpn.
 * Giải phóng node framephy_struct (frame đã được cấp phát, không cần quản lý nữa).
 * Trả về -1 nếu hết frame trống (out of memory). */
int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp = mp->free_fp_list;
   if (fp == NULL)
      return -1; /* hết frame trống */

   *retfpn = fp->fpn;
   mp->free_fp_list = fp->fp_next;
   free(fp); /* giải phóng node quản lý */
   return 0;
}

/* MEMPHY_put_freefp: trả frame fpn về danh sách frame trống.
 * Tạo node mới và thêm vào ĐẦU free_fp_list (O(1)).
 * Được gọi khi: swap in (frame SWAP về RAM), hoặc tiến trình kết thúc. */
int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   struct framephy_struct *fp = mp->free_fp_list;
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));
   newnode->fpn = fpn;
   newnode->fp_next = fp;        /* thêm vào đầu list */
   mp->free_fp_list = newnode;
   return 0;
}

/* MEMPHY_dump: in toàn bộ nội dung bộ nhớ vật lý ra màn hình (hex dump).
 * Dùng để debug — xem byte nào đang ở địa chỉ nào trong RAM/SWAP. */
int MEMPHY_dump(struct memphy_struct *mp)
{
   int i;
   if (mp == NULL || mp->storage == NULL) return -1;
   for (i = 0; i < mp->maxsz; i++) {
      if (i % 16 == 0)
         printf("\n%04x: ", i);
      printf("%02x ", (unsigned char)mp->storage[i]);
   }
   printf("\n");
   return 0;
}

/* init_memphy: khởi tạo thiết bị bộ nhớ vật lý.
 *   1. Cấp phát mảng storage[max_size] (= nội dung RAM/SWAP thực sự)
 *   2. Xóa sạch về 0 (memset)
 *   3. Format: tạo free_fp_list với tất cả frame sẵn sàng
 *   4. Đặt rdmflg: 1=RAM (random), 0=SWAP disk (sequential)
 *
 * Gọi từ main() trong os.c trước khi tạo thread. */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   memset(mp->storage, 0, max_size * sizeof(BYTE));
   MEMPHY_format(mp, PAGING_PAGESZ); /* chia thành frames, tạo free list */
   mp->rdmflg = (randomflg != 0) ? 1 : 0;
   if (!mp->rdmflg)
      mp->cursor = 0; /* sequential device: bắt đầu ở vị trí 0 */
   return 0;
}
